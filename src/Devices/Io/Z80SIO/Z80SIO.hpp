#pragma once
#include "../../../Simulator/Inspect.hpp"
#include "../../../Simulator/Platform.hpp"
#include "../../Serial/Terminal.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Zilog Z80 SIO: dual-channel serial controller with daisy-chain interrupt
// logic. Asynchronous mode is modeled at bit level, or per channel at byte
// level through a byte link (e.g. a TcpTerminal); see Z80SIO.md for behavior,
// timing and limitations.
class Z80SIO final : public Sim::Device, public Sim::IRegisterInspectable {
  public:
	struct ChannelPins {
		Sim::Pin RxD, TxD, RxC, TxC, NotCTS, NotDCD, NotSYNC, NotRTS, NotDTR, WRdy;

		ChannelPins(Sim::Platform* p, const std::string& prefix) :
			RxD(Sim::Pin::New(p, prefix + "RxD")), TxD(Sim::Pin::New(p, prefix + "TxD")),
			RxC(Sim::Pin::New(p, prefix + "RxC")), TxC(Sim::Pin::New(p, prefix + "TxC")),
			NotCTS(Sim::Pin::New(p, prefix + "NotCTS")), NotDCD(Sim::Pin::New(p, prefix + "NotDCD")),
			NotSYNC(Sim::Pin::New(p, prefix + "NotSYNC")), NotRTS(Sim::Pin::New(p, prefix + "NotRTS")),
			NotDTR(Sim::Pin::New(p, prefix + "NotDTR")), WRdy(Sim::Pin::New(p, prefix + "WRdy")) {}
	};

	struct Z80SIOPins {
		Sim::Bus D;
		Sim::Pin BNotA, CNotD, NotCE, CLK, NotM1, NotIORQ, NotRD, NotRESET;
		Sim::Pin IEI, IEO, NotINT;
		ChannelPins A, B;

		Z80SIOPins(Sim::Platform* p, const std::string& name) :
			D(Sim::Bus::New(p, 8, name + ".D")), BNotA(Sim::Pin::New(p, name + ".BNotA")),
			CNotD(Sim::Pin::New(p, name + ".CNotD")), NotCE(Sim::Pin::New(p, name + ".NotCE")),
			CLK(Sim::Pin::New(p, name + ".CLK")), NotM1(Sim::Pin::New(p, name + ".NotM1")),
			NotIORQ(Sim::Pin::New(p, name + ".NotIORQ")), NotRD(Sim::Pin::New(p, name + ".NotRD")),
			NotRESET(Sim::Pin::New(p, name + ".NotRESET")), IEI(Sim::Pin::New(p, name + ".IEI")),
			IEO(Sim::Pin::New(p, name + ".IEO")), NotINT(Sim::Pin::New(p, name + ".NotINT")), A(p, name + ".A."),
			B(p, name + ".B.") {}
	};
	Z80SIOPins Pins;

	// Byte-level transport for one channel, in place of the serial pins (see Z80SIO.md, "Byte link").
	// `transmit` gets each character the channel sends; `ready` is called when the channel's RTS output changes.
	struct ByteLink {
		std::function<void(uint8_t)> transmit;
		std::function<void(bool)> ready;
	};

  private:
	enum class Trig : uint8_t {
		NotRESET,
		NotCE,
		NotIORQ,
		NotRD,
		NotM1,
		BNotA,
		CNotD,
		IEI,
		CLK,
		RxC0, // per channel: RxC, TxC, then the modem inputs; channel B follows channel A
		TxC0,
		Modem0,
		RxC1,
		TxC1,
		Modem1
	};

	enum SerialKind : int {
		KindRxC = 0,
		KindTxC = 1,
		KindModem = 2
	};

	static constexpr Trig SerialTrig(int channel, SerialKind kind) {
		return static_cast<Trig>(static_cast<uint8_t>(Trig::RxC0) + channel * 3 + kind);
	}

	// Interrupt sources of one channel, highest priority first. Source index = channel * 3 + kind.
	enum SourceKind : int {
		SrcRx = 0,
		SrcTx = 1,
		SrcExt = 2
	};
	static constexpr int SourceCount = 6;

	// WR1
	static constexpr uint8_t Wr1ExtEnable = 0x01;
	static constexpr uint8_t Wr1TxEnable = 0x02;
	static constexpr uint8_t Wr1StatusAffectsVector = 0x04;
	static constexpr uint8_t Wr1WaitReadyEnable = 0x80;
	static constexpr uint8_t Wr1ReadyFunction = 0x40;
	static constexpr uint8_t Wr1WaitReadyOnRx = 0x20;
	// WR3
	static constexpr uint8_t Wr3RxEnable = 0x01;
	static constexpr uint8_t Wr3AutoEnables = 0x20;
	// WR4
	static constexpr uint8_t Wr4Parity = 0x01;
	static constexpr uint8_t Wr4EvenParity = 0x02;
	// WR5
	static constexpr uint8_t Wr5Rts = 0x02;
	static constexpr uint8_t Wr5TxEnable = 0x08;
	static constexpr uint8_t Wr5SendBreak = 0x10;
	static constexpr uint8_t Wr5Dtr = 0x80;
	// RR0 bits that follow the External/Status latch
	static constexpr uint8_t Rr0Dcd = 0x08;
	static constexpr uint8_t Rr0Sync = 0x10;
	static constexpr uint8_t Rr0Cts = 0x20;
	static constexpr uint8_t Rr0Underrun = 0x40;
	static constexpr uint8_t Rr0Break = 0x80;

	enum class RxPhase : uint8_t {
		Idle,
		Start,
		Data,
		Parity,
		Stop,
		WaitMark
	};

	struct RxChar {
		uint8_t data = 0;
		bool parityError = false;
		bool framingError = false;
		bool overrun = false;
	};

	struct Channel {
		uint8_t wr1 = 0, wr3 = 0, wr4 = 0, wr5 = 0, wr6 = 0, wr7 = 0;
		uint8_t pointer = 0;

		// Transmitter: buffer, then a frame shifted out one bit per `bitClocks` TxC falling edges.
		bool txBufFull = false;
		uint8_t txBuf = 0;
		bool txActive = false;
		uint16_t txFrame = 0; // bit i of the frame, start bit first
		uint8_t txLen = 0, txIdx = 0, txClk = 0;
		uint8_t txBitClocks = 1, txStopClocks = 1;
		bool txLine = true;
		bool txIntPending = false;
		bool txUnderrun = true; // RR0 D6, set by reset, cleared only by the command
		bool rtsLow = false;
		bool txcLevel = false;

		// Receiver: shift register state, then a three-deep character FIFO.
		RxPhase rxPhase = RxPhase::Idle;
		uint8_t rxNext = 0, rxBit = 0, rxShift = 0;
		bool rxParityBit = false;
		bool rxcLevel = false;
		std::array<RxChar, 3> fifo{};
		uint8_t fifoCount = 0;
		bool stickyParity = false, stickyOverrun = false;
		bool firstArmed = false;
		bool breakActive = false;

		// External/Status: latch, request, and the inputs seen last (true = pin low).
		bool extLatched = false, extPending = false;
		uint8_t extSnapshot = 0;
		bool ctsLow = false, dcdLow = false, syncLow = false;
	};

	std::array<Channel, 2> _ch{};
	std::array<bool, SourceCount> _ius{};
	uint8_t _vector = 0; // WR2
	Sim::Diagnostics& _diagnostics;
	std::string _name;

	// Bus cycle decoded from the pins on every Logic() call.
	bool _isWrite = false, _isRead = false, _isAck = false, _isFetch = false, _ioCycle = false;
	bool _isControl = false;
	int _chSelect = 0;

	bool _writeSeen = false;

	bool _readActive = false, _readCommitted = false;
	int _readChannel = 0;
	bool _readControl = false;
	uint8_t _readValue = 0;

	bool _ackActive = false, _ackDrive = false, _ackSpecial = false;
	int _ackSource = -1; // source the acknowledge will move to service; -1 once committed
	uint8_t _ackVector = 0;

	bool _fetching = false, _sawEd = false;
	uint8_t _fetchByte = 0;

	// Byte link. Bytes reach `_hostRx` from any thread and are moved into the receive FIFO by the sim thread.
	static constexpr size_t HostQueueCapacity = 4;
	std::array<bool, 2> _linked{};
	std::array<ByteLink, 2> _link{};
	std::array<bool, 2> _readyAnnounced{};
	std::mutex _hostMutex;
	std::condition_variable _hostCv;
	std::array<std::deque<uint8_t>, 2> _hostRx;
	std::array<std::atomic<uint8_t>, 2> _hostCount{};
	bool _closing = false;

	// Terminal endpoint of a byte-linked channel, created by HostTerminal().
	class HostPort final : public Terminal {
		Z80SIO& _sio;
		int _ch;

	  protected:
		void Receive(uint8_t b) override {
			_sio.HostReceive(_ch, b, true);
		}

	  public:
		HostPort(Z80SIO& sio, int ch) : _sio(sio), _ch(ch) {}
		void Out(uint8_t b) {
			Send(b);
		}
		void Ready(bool ready) {
			UpdateRTS(ready);
		}
	};
	std::array<std::unique_ptr<HostPort>, 2> _port; // last, so the terminals' threads stop before the queues go

  public:
	Z80SIO(Sim::Platform* p, std::string name = "Z80SIO") :
		Pins(p, name), _diagnostics(p->GetDiagnostics()), _name(std::move(name)) {
		Pins.NotRESET.AddTrigger(this, Trig::NotRESET);
		Pins.NotCE.AddTrigger(this, Trig::NotCE);
		Pins.NotIORQ.AddTrigger(this, Trig::NotIORQ);
		Pins.NotRD.AddTrigger(this, Trig::NotRD);
		Pins.NotM1.AddTrigger(this, Trig::NotM1);
		Pins.BNotA.AddTrigger(this, Trig::BNotA);
		Pins.CNotD.AddTrigger(this, Trig::CNotD);
		Pins.IEI.AddTrigger(this, Trig::IEI);
		Pins.CLK.AddTrigger(this, Trig::CLK, false);
		for (int ch = 0; ch < 2; ++ch) {
			auto& pins = ChannelPinsOf(ch);
			pins.RxC.AddTrigger(this, SerialTrig(ch, KindRxC), false);
			pins.TxC.AddTrigger(this, SerialTrig(ch, KindTxC), false);
			pins.NotCTS.AddTrigger(this, SerialTrig(ch, KindModem), false);
			pins.NotDCD.AddTrigger(this, SerialTrig(ch, KindModem), false);
			pins.NotSYNC.AddTrigger(this, SerialTrig(ch, KindModem), false);
		}
	}

	~Z80SIO() {
		{
			std::lock_guard<std::mutex> lock(_hostMutex);
			_closing = true;
		}
		_hostCv.notify_all();
	}

	// Moves a channel to a byte-level transport: its serial clocks and data pins are ignored, characters the CPU
	// writes go to `link.transmit`, and characters offered through HostReceive() reach the receive FIFO. Call
	// before the simulation starts.
	void BindByteLink(int ch, ByteLink link) {
		_link[ch] = std::move(link);
		_linked[ch] = true;
		_readyAnnounced[ch] = false;
	}

	// The channel's endpoint for Terminal::NullModemConnect (e.g. with a TcpTerminal). Binds the channel to it.
	// Its clear-to-send peer is told when the channel's RTS output changes; the peer's own RTS is not read.
	::Terminal& HostTerminal(int ch) {
		if (!_port[ch]) {
			_port[ch] = std::make_unique<HostPort>(*this, ch);
			HostPort* port = _port[ch].get();
			BindByteLink(ch, {[port](uint8_t b) { port->Out(b); }, [port](bool ready) { port->Ready(ready); }});
		}
		return *_port[ch];
	}

	// Offers a received character to a byte-linked channel from any thread. With `wait` it blocks until there
	// is room (never call it that way from the sim thread); without, it returns false when the queue is full.
	bool HostReceive(int ch, uint8_t b, bool wait = false) {
		std::unique_lock<std::mutex> lock(_hostMutex);
		if (wait) _hostCv.wait(lock, [&] { return _closing || _hostRx[ch].size() < HostQueueCapacity; });
		if (_closing || _hostRx[ch].size() >= HostQueueCapacity) return false;
		_hostRx[ch].push_back(b);
		_hostCount[ch].store(static_cast<uint8_t>(_hostRx[ch].size()), std::memory_order_relaxed);
		return true;
	}

	void Logic(Sim::TriggerMask mask) {
		// Reset is a level, not an edge: read the pin on every call.
		if (Pins.NotRESET.IsLow()) {
			Reset();
			DriveOutputs();
			return;
		}

		DecodeBusCycle();

		if (mask.IsSet(Trig::CLK) && Pins.CLK.IsHigh()) OnRisingEdge();

		if (mask.IsSetAny(Trig::RxC0, Trig::TxC0, Trig::Modem0, Trig::RxC1, Trig::TxC1, Trig::Modem1)) {
			for (int ch = 0; ch < 2; ++ch) {
				if (mask.IsSet(SerialTrig(ch, KindRxC))) SampleRxClock(ch);
				if (mask.IsSet(SerialTrig(ch, KindTxC))) SampleTxClock(ch);
				if (mask.IsSet(SerialTrig(ch, KindModem))) SampleModem(ch);
			}
		}

		DriveOutputs();
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		std::vector<std::pair<std::string, uint64_t>> regs;
		regs.emplace_back("Vector", _vector);
		uint64_t ius = 0, requests = 0;
		for (int i = 0; i < SourceCount; ++i) {
			ius |= static_cast<uint64_t>(_ius[i]) << i;
			requests |= static_cast<uint64_t>(Requesting(i / 3, i % 3)) << i;
		}
		regs.emplace_back("InService", ius);
		regs.emplace_back("Requests", requests);
		for (int i = 0; i < 2; ++i) {
			const auto& c = _ch[i];
			const std::string prefix = i == 0 ? "A." : "B.";
			regs.emplace_back(prefix + "Pointer", c.pointer);
			regs.emplace_back(prefix + "WR1", c.wr1);
			regs.emplace_back(prefix + "WR3", c.wr3);
			regs.emplace_back(prefix + "WR4", c.wr4);
			regs.emplace_back(prefix + "WR5", c.wr5);
			regs.emplace_back(prefix + "WR6", c.wr6);
			regs.emplace_back(prefix + "WR7", c.wr7);
			regs.emplace_back(prefix + "RR0", ReadRR0(i));
			regs.emplace_back(prefix + "RR1", ReadRR1(c));
			regs.emplace_back(prefix + "RxCount", c.fifoCount);
			regs.emplace_back(prefix + "TxBufFull", c.txBufFull);
			regs.emplace_back(prefix + "TxActive", c.txActive);
			regs.emplace_back(prefix + "Break", c.breakActive);
			regs.emplace_back(prefix + "ByteLinked", _linked[i]);
			regs.emplace_back(prefix + "HostQueued", _hostCount[i].load(std::memory_order_relaxed));
		}
		return regs;
	}

  private:
	ChannelPins& ChannelPinsOf(int ch) {
		return ch == 0 ? Pins.A : Pins.B;
	}

	static bool IsAsync(const Channel& c) {
		return (c.wr4 & 0x0C) != 0;
	}

	static int ClockMultiplier(const Channel& c) {
		static constexpr int multiplier[4] = {1, 16, 32, 64};
		return multiplier[c.wr4 >> 6];
	}

	static int RxBits(const Channel& c) {
		static constexpr int bits[4] = {5, 7, 6, 8};
		return bits[c.wr3 >> 6];
	}

	static bool AllSent(const Channel& c) {
		return !c.txActive && !c.txBufFull;
	}

	bool RxActive(int ch) {
		const auto& c = _ch[ch];
		return (c.wr3 & Wr3RxEnable) && (!(c.wr3 & Wr3AutoEnables) || ChannelPinsOf(ch).NotDCD.IsLow());
	}

	bool TxEnabled(int ch) {
		const auto& c = _ch[ch];
		return (c.wr5 & Wr5TxEnable) && (!(c.wr3 & Wr3AutoEnables) || ChannelPinsOf(ch).NotCTS.IsLow());
	}

	void Reset() {
		for (int ch = 0; ch < 2; ++ch)
			ResetChannel(ch);
		_ius.fill(false);
		_writeSeen = _readActive = _readCommitted = _ackActive = _ackDrive = false;
		_ackSource = -1;
		_fetching = _sawEd = false;
		_isWrite = _isRead = _isAck = _isFetch = _ioCycle = false;
	}

	void ResetChannel(int ch) {
		auto& c = _ch[ch];
		c = Channel{};
		const auto& pins = ChannelPinsOf(ch);
		c.rxcLevel = pins.RxC.IsHigh();
		c.txcLevel = pins.TxC.IsHigh();
		c.ctsLow = pins.NotCTS.IsLow();
		c.dcdLow = pins.NotDCD.IsLow();
		c.syncLow = pins.NotSYNC.IsLow();
		for (int kind = 0; kind < 3; ++kind)
			_ius[ch * 3 + kind] = false;
	}

	// ---- Bus cycles --------------------------------------------------------

	void DecodeBusCycle() {
		const bool iorq = Pins.NotIORQ.IsLow();
		const bool m1 = Pins.NotM1.IsLow();
		const bool rd = Pins.NotRD.IsLow();
		_ioCycle = iorq && !m1 && Pins.NotCE.IsLow();
		_isWrite = _ioCycle && !rd;
		_isRead = _ioCycle && rd;
		_isAck = iorq && m1;
		_isFetch = m1 && rd && !iorq;
		_isControl = Pins.CNotD.IsHigh();
		_chSelect = Pins.BNotA.IsHigh() ? 1 : 0;

		if (!_isWrite) _writeSeen = false;

		// Latch what a read will drive when its strobe starts (or its selection
		// changes), so the value stays put if a character arrives while it is held.
		if (_isRead) {
			if (!_readActive || _readChannel != _chSelect || _readControl != _isControl) {
				_readActive = true;
				_readChannel = _chSelect;
				_readControl = _isControl;
				_readValue = ReadRegister(_chSelect, _isControl);
			}
		} else {
			_readActive = _readCommitted = false;
		}

		if (_isAck) {
			if (!_ackActive) {
				_ackActive = true;
				const int source = Pins.IEI.IsHigh() ? RequestingSource() : -1;
				if (source >= 0) {
					_ackSource = source;
					_ackDrive = true;
					Requesting(source / 3, source % 3, &_ackSpecial);
					_ackVector = VectorFor(source, _ackSpecial);
				}
			}
		} else {
			_ackActive = _ackDrive = false;
			_ackSource = -1;
		}
	}

	void OnRisingEdge() {
		CommitAcknowledge();
		CommitRead();
		ObserveFetch();
		for (int ch = 0; ch < 2; ++ch) {
			if (_linked[ch]) ServiceByteLink(ch);
		}
		// Last, so a write takes effect from the next edge.
		if (_isWrite && !_writeSeen) {
			_writeSeen = true;
			const uint8_t data = Pins.D.Read<uint8_t>();
			if (_isControl) WriteControl(_chSelect, data);
			else WriteData(_chSelect, data);
		}
	}

	uint8_t ReadRegister(int ch, bool control) const {
		const auto& c = _ch[ch];
		if (!control) return c.fifoCount ? c.fifo[0].data : 0;
		switch (c.pointer) {
			case 1: return ReadRR1(c);
			case 2:
				if (ch == 1) return ReadRR2();
				[[fallthrough]];
			default: return ReadRR0(ch);
		}
	}

	uint8_t LiveExternalStatus(int ch) const {
		const auto& c = _ch[ch];
		return static_cast<uint8_t>(
			(c.dcdLow ? Rr0Dcd : 0) | (c.syncLow ? Rr0Sync : 0) | (c.ctsLow ? Rr0Cts : 0) |
			(c.txUnderrun ? Rr0Underrun : 0) | (c.breakActive ? Rr0Break : 0)
		);
	}

	uint8_t ReadRR0(int ch) const {
		const auto& c = _ch[ch];
		uint8_t value = c.extLatched ? c.extSnapshot : LiveExternalStatus(ch);
		if (c.fifoCount) value |= 0x01;
		if (ch == 0 && PendingSource(true) >= 0) value |= 0x02;
		if (!c.txBufFull) value |= 0x04;
		return value;
	}

	static uint8_t ReadRR1(const Channel& c) {
		uint8_t value = AllSent(c) ? 0x01 : 0x00;
		const bool parity = c.stickyParity || (c.fifoCount && c.fifo[0].parityError);
		const bool overrun = c.stickyOverrun || (c.fifoCount && c.fifo[0].overrun);
		const bool framing = c.fifoCount && c.fifo[0].framingError;
		if (parity) value |= 0x10;
		if (overrun) value |= 0x20;
		if (framing) value |= 0x40;
		return value;
	}

	// Channel B's vector register as the CPU would receive it for the highest-priority request now.
	uint8_t ReadRR2() const {
		if (!(_ch[1].wr1 & Wr1StatusAffectsVector)) return _vector;
		const int source = PendingSource(true);
		if (source < 0) return static_cast<uint8_t>((_vector & 0xF1) | (3 << 1));
		bool special = false;
		Requesting(source / 3, source % 3, &special);
		return VectorFor(source, special);
	}

	void CommitRead() {
		if (!_isRead || !_readActive || _readCommitted) return;
		_readCommitted = true;
		auto& c = _ch[_readChannel];
		if (_readControl) {
			c.pointer = 0;
			return;
		}
		if (c.fifoCount == 0 || HoldsForErrorReset(c)) return;
		const RxChar head = c.fifo[0];
		if (head.parityError) c.stickyParity = true;
		if (head.overrun) c.stickyOverrun = true;
		for (int i = 1; i < c.fifoCount; ++i)
			c.fifo[i - 1] = c.fifo[i];
		--c.fifoCount;
	}

	// In interrupt-on-first-character mode a special condition freezes the receive buffer until Error Reset.
	static bool HoldsForErrorReset(const Channel& c) {
		return ((c.wr1 >> 3) & 3) == 1 && c.fifoCount && (c.fifo[0].framingError || c.fifo[0].overrun);
	}

	void WriteData(int ch, uint8_t data) {
		auto& c = _ch[ch];
		c.txBuf = data;
		c.txBufFull = true;
		c.txIntPending = false;
		_diagnostics.Println("{} ch{} data {:02x}", _name, ch, data);
	}

	void WriteControl(int ch, uint8_t data) {
		auto& c = _ch[ch];
		if (c.pointer == 0) {
			DoCommand(ch, (data >> 3) & 7);
			if ((data >> 6) == 3) c.txUnderrun = false;
			// After the command, so a channel reset does not discard the pointer bits sent with it.
			c.pointer = data & 7;
			_diagnostics.Println("{} ch{} WR0 {:02x}", _name, ch, data);
			return;
		}
		const uint8_t reg = c.pointer;
		c.pointer = 0;
		WriteRegister(ch, reg, data);
		_diagnostics.Println("{} ch{} WR{} {:02x}", _name, ch, reg, data);
	}

	void WriteRegister(int ch, uint8_t reg, uint8_t data) {
		auto& c = _ch[ch];
		switch (reg) {
			case 1: {
				const bool toFirstCharMode = ((data >> 3) & 3) == 1 && ((c.wr1 >> 3) & 3) != 1;
				c.wr1 = data;
				if (toFirstCharMode) c.firstArmed = true;
				if (!(data & Wr1TxEnable)) c.txIntPending = false;
				if (!(data & Wr1ExtEnable)) c.extLatched = c.extPending = false;
				break;
			}
			case 2:
				if (ch == 1) _vector = data;
				break;
			case 3: c.wr3 = data; break;
			case 4: c.wr4 = data; break;
			case 5: c.wr5 = data; break;
			case 6: c.wr6 = data; break;
			case 7: c.wr7 = data; break;
			default: break;
		}
	}

	void DoCommand(int ch, uint8_t command) {
		auto& c = _ch[ch];
		switch (command) {
			case 2: // Reset External/Status Interrupts
				c.extLatched = c.extPending = false;
				break;
			case 3: // Channel Reset
				ResetChannel(ch);
				if (ch == 0) _ius.fill(false);
				break;
			case 4: // Enable Interrupt on Next Receive Character
				c.firstArmed = true;
				break;
			case 5: // Reset Transmitter Interrupt Pending
				c.txIntPending = false;
				break;
			case 6: // Error Reset
				c.stickyParity = c.stickyOverrun = false;
				if (c.fifoCount) c.fifo[0].parityError = c.fifo[0].framingError = c.fifo[0].overrun = false;
				break;
			case 7: // Return from Interrupt, channel A only
				if (ch == 0) EndHighestService();
				break;
			default: // Null; Send Abort has no meaning in asynchronous modes
				break;
		}
	}

	// ---- Interrupts --------------------------------------------------------

	// Whether one source is asking for service. `special` is set for a receive request that reports a special
	// condition.
	bool Requesting(int ch, int kind, bool* special = nullptr) const {
		const auto& c = _ch[ch];
		if (special) *special = false;
		switch (kind) {
			case SrcRx: {
				const int mode = (c.wr1 >> 3) & 3;
				if (mode == 0 || c.fifoCount == 0) return false;
				const RxChar& head = c.fifo[0];
				const bool isSpecial = head.framingError || head.overrun || (mode == 2 && head.parityError);
				if (special) *special = isSpecial;
				return isSpecial || mode != 1 || c.firstArmed;
			}
			case SrcTx: return c.txIntPending && (c.wr1 & Wr1TxEnable);
			default: return c.extPending && (c.wr1 & Wr1ExtEnable);
		}
	}

	// Highest-priority source that is asking, -1 if none. With `ignoreService` a source under service does not hide it.
	int PendingSource(bool ignoreService) const {
		for (int i = 0; i < SourceCount; ++i) {
			if (!ignoreService && _ius[i]) return -1;
			if (Requesting(i / 3, i % 3)) return i;
		}
		return -1;
	}

	// Highest-priority source the CPU can accept: a source under service blocks itself and every lower one.
	int RequestingSource() const {
		return PendingSource(false);
	}

	uint8_t VectorFor(int source, bool special) const {
		if (!(_ch[1].wr1 & Wr1StatusAffectsVector)) return _vector;
		const int ch = source / 3;
		int code = 0;
		switch (source % 3) {
			case SrcTx: code = 0; break;
			case SrcExt: code = 1; break;
			default: code = special ? 3 : 2; break;
		}
		if (ch == 0) code |= 4;
		return static_cast<uint8_t>((_vector & 0xF1) | (code << 1));
	}

	void CommitAcknowledge() {
		if (_ackSource < 0) return;
		const int ch = _ackSource / 3;
		auto& c = _ch[ch];
		_ius[_ackSource] = true;
		switch (_ackSource % 3) {
			case SrcRx:
				if (!_ackSpecial && ((c.wr1 >> 3) & 3) == 1) c.firstArmed = false;
				break;
			case SrcTx: c.txIntPending = false; break;
			default: c.extPending = false; break;
		}
		_diagnostics.Println("{} interrupt acknowledged: source {} vector {:02x}", _name, _ackSource, _ackVector);
		_ackSource = -1;
	}

	void EndHighestService() {
		for (int i = 0; i < SourceCount; ++i) {
			if (_ius[i]) {
				_ius[i] = false;
				_diagnostics.Println("{} service ended: source {}", _name, i);
				return;
			}
		}
	}

	// Opcode bytes are sampled on rising edges while M1 and RD are low; the
	// byte seen on the last such edge is the one the CPU latched, and is
	// decoded once the fetch has ended.
	void ObserveFetch() {
		if (_isFetch) {
			_fetchByte = Pins.D.Read<uint8_t>();
			_fetching = true;
			return;
		}
		if (!_fetching) return;
		_fetching = false;
		if (_sawEd && _fetchByte == 0x4D && Pins.IEI.IsHigh()) EndHighestService();
		_sawEd = _fetchByte == 0xED;
	}

	// External/Status change on one of the monitored conditions: capture and request, unless already latched.
	void ExternalStatusChange(int ch) {
		auto& c = _ch[ch];
		if (!(c.wr1 & Wr1ExtEnable) || c.extLatched) return;
		c.extLatched = true;
		c.extPending = true;
		c.extSnapshot = LiveExternalStatus(ch);
		_diagnostics.Println("{} ch{} external/status change {:02x}", _name, ch, c.extSnapshot);
	}

	void SampleModem(int ch) {
		auto& c = _ch[ch];
		const auto& pins = ChannelPinsOf(ch);
		const bool cts = pins.NotCTS.IsLow(), dcd = pins.NotDCD.IsLow(), sync = pins.NotSYNC.IsLow();
		const bool changed = cts != c.ctsLow || dcd != c.dcdLow || sync != c.syncLow;
		c.ctsLow = cts;
		c.dcdLow = dcd;
		c.syncLow = sync;
		if (changed) ExternalStatusChange(ch);
	}

	// ---- Receiver ----------------------------------------------------------

	void SampleRxClock(int ch) {
		if (_linked[ch]) return;
		auto& c = _ch[ch];
		const bool level = ChannelPinsOf(ch).RxC.IsHigh();
		const bool rising = level && !c.rxcLevel;
		c.rxcLevel = level;
		if (rising) RxClockEdge(ch);
	}

	void RxClockEdge(int ch) {
		auto& c = _ch[ch];
		if (!IsAsync(c) || !RxActive(ch)) {
			c.rxPhase = RxPhase::Idle;
			c.breakActive = false;
			return;
		}
		const bool rxd = ChannelPinsOf(ch).RxD.IsHigh();
		const int n = ClockMultiplier(c);

		if (c.rxPhase == RxPhase::Idle) {
			if (rxd) return;
			// x1 has no way to find mid-bit: the first low sample is taken as a valid start bit.
			if (n == 1) {
				c.rxPhase = RxPhase::Data;
				c.rxBit = 0;
				c.rxShift = 0;
				c.rxNext = 1;
			} else {
				c.rxPhase = RxPhase::Start;
				c.rxNext = static_cast<uint8_t>(n / 2);
			}
			return;
		}
		if (c.rxPhase == RxPhase::WaitMark) {
			if (!rxd) return;
			c.rxPhase = RxPhase::Idle;
			if (c.breakActive) {
				c.breakActive = false;
				ExternalStatusChange(ch);
			}
			return;
		}
		if (--c.rxNext != 0) return;

		switch (c.rxPhase) {
			case RxPhase::Start:
				if (rxd) { // a glitch, not a start bit
					c.rxPhase = RxPhase::Idle;
					return;
				}
				c.rxPhase = RxPhase::Data;
				c.rxBit = 0;
				c.rxShift = 0;
				c.rxNext = static_cast<uint8_t>(n);
				break;
			case RxPhase::Data:
				if (rxd) c.rxShift |= static_cast<uint8_t>(1u << c.rxBit);
				if (++c.rxBit == RxBits(c)) c.rxPhase = (c.wr4 & Wr4Parity) ? RxPhase::Parity : RxPhase::Stop;
				c.rxNext = static_cast<uint8_t>(n);
				break;
			case RxPhase::Parity:
				c.rxParityBit = rxd;
				c.rxPhase = RxPhase::Stop;
				c.rxNext = static_cast<uint8_t>(n);
				break;
			default: FinishCharacter(ch, !rxd); break;
		}
	}

	void FinishCharacter(int ch, bool framingError) {
		auto& c = _ch[ch];
		c.rxPhase = framingError ? RxPhase::WaitMark : RxPhase::Idle;

		const int bits = RxBits(c);
		RxChar received;
		received.data = c.rxShift;
		received.framingError = framingError;
		int used = bits; // bit positions holding data or parity; the rest read as ones
		if (c.wr4 & Wr4Parity) {
			const bool odd = (std::popcount(static_cast<unsigned>(c.rxShift)) + (c.rxParityBit ? 1 : 0)) & 1;
			received.parityError = (c.wr4 & Wr4EvenParity) ? odd : !odd;
			// Characters shorter than eight bits carry the parity bit just above the data; eight-bit ones drop it.
			if (bits < 8) {
				if (c.rxParityBit) received.data |= static_cast<uint8_t>(1u << bits);
				++used;
			}
		}
		if (used < 8) received.data |= static_cast<uint8_t>(0xFF << used);

		if (c.fifoCount == 3) {
			received.overrun = true;
			c.fifo[2] = received;
		} else {
			c.fifo[c.fifoCount++] = received;
		}
		_diagnostics.Println(
			"{} ch{} received {:02x}{}{}",
			_name,
			ch,
			received.data,
			framingError ? " framing" : "",
			received.parityError ? " parity" : ""
		);

		if (framingError && c.rxShift == 0 && !c.breakActive) {
			c.breakActive = true;
			ExternalStatusChange(ch);
		}
	}

	// ---- Transmitter -------------------------------------------------------

	void SampleTxClock(int ch) {
		if (_linked[ch]) return;
		auto& c = _ch[ch];
		const bool level = ChannelPinsOf(ch).TxC.IsHigh();
		const bool falling = !level && c.txcLevel;
		c.txcLevel = level;
		if (falling) TxClockEdge(ch);
	}

	void TxClockEdge(int ch) {
		auto& c = _ch[ch];
		if (!IsAsync(c)) {
			c.txLine = true;
			return;
		}
		if (c.txActive) {
			const uint8_t duration = c.txIdx + 1 == c.txLen ? c.txStopClocks : c.txBitClocks;
			if (c.txClk < duration) {
				++c.txClk;
				return;
			}
			if (++c.txIdx < c.txLen) {
				c.txLine = (c.txFrame >> c.txIdx) & 1;
				c.txClk = 1;
				return;
			}
			c.txActive = false;
			c.txLine = true;
		}
		if (c.txBufFull && TxEnabled(ch)) StartFrame(ch);
	}

	// Data bits sent for a written byte. "Five or fewer" counts the leading ones above the data.
	static int TxBits(const Channel& c, uint8_t value) {
		switch ((c.wr5 >> 5) & 3) {
			case 1: return 7;
			case 2: return 6;
			case 3: return 8;
			default:
				return (value & 0xF0) == 0xF0	? 1
					   : (value & 0xE0) == 0xE0 ? 2
					   : (value & 0xC0) == 0xC0 ? 3
					   : (value & 0x80)			? 4
												: 5;
		}
	}

	// Byte-linked channel, on each rising system clock: send the buffered character at once, and move
	// host-received characters into the FIFO. Characters arriving while the receiver is off are dropped.
	void ServiceByteLink(int ch) {
		auto& c = _ch[ch];
		if (c.txBufFull && IsAsync(c) && TxEnabled(ch)) {
			const uint8_t value = c.txBuf;
			c.txBufFull = false;
			if (c.wr1 & Wr1TxEnable) c.txIntPending = true;
			const int bits = TxBits(c, value);
			if (_link[ch].transmit) _link[ch].transmit(static_cast<uint8_t>(value & ((1u << bits) - 1)));
			_diagnostics.Println("{} ch{} byte out {:02x}", _name, ch, value);
		}

		if (_hostCount[ch].load(std::memory_order_relaxed) == 0) return;
		std::unique_lock<std::mutex> lock(_hostMutex);
		auto& queue = _hostRx[ch];
		const bool accept = IsAsync(c) && RxActive(ch);
		while (!queue.empty()) {
			if (accept) {
				if (c.fifoCount == 3) break;
				// Characters shorter than eight bits read back with ones in the unused high bits.
				const int bits = RxBits(c);
				uint8_t data = queue.front();
				if (bits < 8) data = static_cast<uint8_t>((data & ((1u << bits) - 1)) | (0xFFu << bits));
				RxChar received;
				received.data = data;
				c.fifo[c.fifoCount++] = received;
				_diagnostics.Println("{} ch{} byte in {:02x}", _name, ch, data);
			}
			queue.pop_front();
		}
		_hostCount[ch].store(static_cast<uint8_t>(queue.size()), std::memory_order_relaxed);
		lock.unlock();
		_hostCv.notify_all();
	}

	void StartFrame(int ch) {
		auto& c = _ch[ch];
		const uint8_t value = c.txBuf;
		c.txBufFull = false;
		if (c.wr1 & Wr1TxEnable) c.txIntPending = true;

		const int bits = TxBits(c, value);
		const unsigned data = value & ((1u << bits) - 1);

		uint16_t frame = 0; // start bit is 0
		frame |= static_cast<uint16_t>(data << 1);
		int length = 1 + bits;
		if (c.wr4 & Wr4Parity) {
			const bool ones = std::popcount(data) & 1;
			const bool parityBit = (c.wr4 & Wr4EvenParity) ? ones : !ones;
			frame |= static_cast<uint16_t>(parityBit) << length;
			++length;
		}
		frame |= static_cast<uint16_t>(1u << length); // stop bit
		++length;

		const int n = ClockMultiplier(c);
		const int stopBits = (c.wr4 >> 2) & 3;
		c.txFrame = frame;
		c.txLen = static_cast<uint8_t>(length);
		c.txIdx = 0;
		c.txClk = 1;
		c.txBitClocks = static_cast<uint8_t>(n);
		c.txStopClocks = static_cast<uint8_t>(stopBits == 1 ? n : stopBits == 2 ? n * 3 / 2 : n * 2);
		c.txLine = false;
		c.txActive = true;
		_diagnostics.Println("{} ch{} transmit {:02x}", _name, ch, value);
	}

	// ---- Outputs -----------------------------------------------------------

	void DriveOutputs() {
		if (_ackDrive) Pins.D.Write(_ackVector);
		else if (_isRead) Pins.D.Write(_readValue);
		else Pins.D.Clear();

		bool busy = false;
		for (int i = 0; i < SourceCount; ++i)
			busy |= _ius[i] || Requesting(i / 3, i % 3);
		const bool iei = Pins.IEI.IsHigh();
		Pins.NotINT.Write(Sim::LogicState::TriLow(iei && RequestingSource() >= 0));
		Pins.IEO.Write(Sim::LogicState::ActiveHigh(iei && !busy));

		for (int ch = 0; ch < 2; ++ch) {
			auto& c = _ch[ch];
			auto& pins = ChannelPinsOf(ch);
			// Without a request to end it, RTS stays active until the transmitter has emptied.
			if (c.wr5 & Wr5Rts) c.rtsLow = true;
			else if (AllSent(c)) c.rtsLow = false;
			pins.NotRTS.Write(Sim::LogicState::ActiveLow(c.rtsLow));
			if (_linked[ch] && _readyAnnounced[ch] != c.rtsLow) {
				_readyAnnounced[ch] = c.rtsLow;
				if (_link[ch].ready) _link[ch].ready(c.rtsLow);
			}
			pins.NotDTR.Write(Sim::LogicState::ActiveLow(c.wr5 & Wr5Dtr));
			pins.TxD.Write(Sim::LogicState::ActiveHigh(c.txLine && !(c.wr5 & Wr5SendBreak)));
			pins.WRdy.Write(WaitReadyLevel(c));
		}
	}

	Sim::LogicState WaitReadyLevel(const Channel& c) const {
		const bool ready = c.wr1 & Wr1ReadyFunction;
		if (!(c.wr1 & Wr1WaitReadyEnable)) return ready ? Sim::LogicState::H : Sim::LogicState::F;
		const bool onRx = c.wr1 & Wr1WaitReadyOnRx;
		if (ready) {
			// Ready is active (low) while the buffer can be served, and drops out for any CPU transfer.
			const bool serviceable = onRx ? c.fifoCount != 0 : !c.txBufFull;
			return Sim::LogicState::ActiveLow(serviceable && !_ioCycle);
		}
		// Wait: a data transfer that the buffer cannot satisfy yet, whichever channel it addresses.
		const bool dataCycle = !_isControl;
		const bool stalled = onRx ? (_isRead && c.fifoCount == 0) : (_isWrite && c.txBufFull);
		return Sim::LogicState::TriLow(dataCycle && stalled);
	}
};
