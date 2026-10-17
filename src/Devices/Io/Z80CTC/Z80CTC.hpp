#pragma once
#include "../../../Simulator/Inspect.hpp"
#include "../../../Simulator/Platform.hpp"
#include <array>
#include <cstdint>
#include <format>
#include <string>
#include <utility>
#include <vector>

// Zilog Z80 CTC: four independent counter/timer channels with daisy-chain
// interrupt logic. See Z80CTC.md for behavior, timing and limitations.
class Z80CTC final : public Sim::Device, public Sim::IRegisterInspectable {
  public:
	struct Z80CTCPins {
		Sim::Bus D;
		Sim::Pin CS0, CS1, NotCE, CLK, NotM1, NotIORQ, NotRD, NotRESET;
		Sim::Pin IEI, IEO, NotINT;
		std::array<Sim::Pin, 4> ClkTrg;
		std::array<Sim::Pin, 3> ZcTo;
	};
	Z80CTCPins Pins;

	// Channel control word bits (D0 = 1 selects a control word).
	static constexpr uint8_t CtrlControl = 0x01;
	static constexpr uint8_t CtrlReset = 0x02;
	static constexpr uint8_t CtrlTcFollows = 0x04;
	static constexpr uint8_t CtrlTrigger = 0x08;
	static constexpr uint8_t CtrlRising = 0x10;
	static constexpr uint8_t CtrlPrescale256 = 0x20;
	static constexpr uint8_t CtrlCounter = 0x40;
	static constexpr uint8_t CtrlInterrupt = 0x80;

  private:
	enum class Trig : uint8_t {
		NotRESET,
		NotCE,
		NotIORQ,
		NotRD,
		NotM1,
		CS0,
		CS1,
		IEI,
		CLK,
		ClkTrg0,
		ClkTrg1,
		ClkTrg2,
		ClkTrg3
	};

	static constexpr Trig ClkTrgBit(int channel) {
		return static_cast<Trig>(static_cast<uint8_t>(Trig::ClkTrg0) + channel);
	}

	struct Channel {
		uint8_t control = 0;
		uint8_t timeConstant = 0; // 0 means 256
		uint16_t count = 0;		  // down-counter, 1..256 once loaded
		uint16_t prescaler = 0;
		bool running = false;	  // a time constant has been loaded since the last reset
		bool waitTrigger = false; // timer loaded, waiting for its trigger edge
		bool startDelay = false;  // trigger seen, prescaler starts on the next edge
		bool awaitTc = false;	  // next write to this channel is the time constant
		bool triggerPending = false;
		bool triggerLevel = false;
		bool zc = false;
		bool intPending = false;
		bool intService = false;
	};

	std::array<Channel, 4> _ch{};
	uint8_t _vector = 0;
	Sim::Diagnostics& _diagnostics;
	std::string _name;

	// Bus cycle decoded from the pins on every Logic() call.
	bool _isWrite = false, _isRead = false, _isAck = false, _isFetch = false;
	int _channelSelect = 0;

	bool _writeSeen = false;

	bool _readActive = false;
	int _readChannel = 0;
	uint8_t _readValue = 0;

	bool _ackActive = false;
	bool _ackDrive = false;
	int _ackChannel = -1; // channel whose request the acknowledge will move to service; -1 once committed
	uint8_t _ackVector = 0;

	bool _fetching = false;
	bool _sawEd = false;
	uint8_t _fetchByte = 0;

  public:
	Z80CTC(Sim::Platform* p, std::string name = "Z80CTC") :
		Pins{
			Sim::Bus::New(p, 8, name + ".D"),
			Sim::Pin::New(p, name + ".CS0"),
			Sim::Pin::New(p, name + ".CS1"),
			Sim::Pin::New(p, name + ".NotCE"),
			Sim::Pin::New(p, name + ".CLK"),
			Sim::Pin::New(p, name + ".NotM1"),
			Sim::Pin::New(p, name + ".NotIORQ"),
			Sim::Pin::New(p, name + ".NotRD"),
			Sim::Pin::New(p, name + ".NotRESET"),
			Sim::Pin::New(p, name + ".IEI"),
			Sim::Pin::New(p, name + ".IEO"),
			Sim::Pin::New(p, name + ".NotINT"),
			{Sim::Pin::New(p, name + ".ClkTrg0"),
			 Sim::Pin::New(p, name + ".ClkTrg1"),
			 Sim::Pin::New(p, name + ".ClkTrg2"),
			 Sim::Pin::New(p, name + ".ClkTrg3")},
			{Sim::Pin::New(p, name + ".ZcTo0"), Sim::Pin::New(p, name + ".ZcTo1"), Sim::Pin::New(p, name + ".ZcTo2")}
		},
		_diagnostics(p->GetDiagnostics()), _name(std::move(name)) {
		Pins.NotRESET.AddTrigger(this, Trig::NotRESET);
		Pins.NotCE.AddTrigger(this, Trig::NotCE);
		Pins.NotIORQ.AddTrigger(this, Trig::NotIORQ);
		Pins.NotRD.AddTrigger(this, Trig::NotRD);
		Pins.NotM1.AddTrigger(this, Trig::NotM1);
		Pins.CS0.AddTrigger(this, Trig::CS0);
		Pins.CS1.AddTrigger(this, Trig::CS1);
		Pins.IEI.AddTrigger(this, Trig::IEI);
		Pins.CLK.AddTrigger(this, Trig::CLK, false);
		for (int i = 0; i < 4; ++i) {
			Pins.ClkTrg[i].AddTrigger(this, ClkTrgBit(i), false);
		}
	}

	void Logic(Sim::TriggerMask mask) {
		// Reset is a level, not an edge: read the pin on every call.
		if (Pins.NotRESET.IsLow()) {
			Reset();
			DriveOutputs();
			return;
		}

		DecodeBusCycle();

		if (mask.IsSet(Trig::CLK)) {
			if (Pins.CLK.IsHigh()) OnRisingEdge();
			else OnFallingEdge();
		}
		// After the clock edge: a trigger edge in the same cycle as a clock edge
		// is counted on the next one.
		if (mask.IsSetAny(Trig::ClkTrg0, Trig::ClkTrg1, Trig::ClkTrg2, Trig::ClkTrg3)) {
			SampleTriggers(mask);
		}

		DriveOutputs();
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		std::vector<std::pair<std::string, uint64_t>> regs;
		regs.emplace_back("Vector", _vector & 0xF8);
		for (int i = 0; i < 4; ++i) {
			const auto& ch = _ch[i];
			const auto prefix = std::format("Ch{}.", i);
			regs.emplace_back(prefix + "Control", ch.control);
			regs.emplace_back(prefix + "TimeConstant", ch.timeConstant);
			regs.emplace_back(prefix + "Count", ch.count);
			regs.emplace_back(prefix + "Prescaler", ch.prescaler);
			regs.emplace_back(prefix + "Running", ch.running);
			regs.emplace_back(prefix + "AwaitTc", ch.awaitTc);
			regs.emplace_back(prefix + "IntPending", ch.intPending);
			regs.emplace_back(prefix + "IntService", ch.intService);
		}
		return regs;
	}

  private:
	static uint16_t TimeConstantValue(const Channel& ch) {
		return ch.timeConstant ? ch.timeConstant : 256;
	}

	void Reset() {
		for (int i = 0; i < 4; ++i) {
			_ch[i] = Channel{};
			_ch[i].triggerLevel = Pins.ClkTrg[i].IsHigh();
		}
		_writeSeen = _readActive = _ackActive = _ackDrive = false;
		_ackChannel = -1;
		_fetching = _sawEd = false;
		_isWrite = _isRead = _isAck = _isFetch = false;
	}

	void DecodeBusCycle() {
		const bool iorq = Pins.NotIORQ.IsLow();
		const bool m1 = Pins.NotM1.IsLow();
		const bool rd = Pins.NotRD.IsLow();
		const bool io = iorq && !m1 && Pins.NotCE.IsLow();
		_isWrite = io && !rd;
		_isRead = io && rd;
		_isAck = iorq && m1;
		_isFetch = m1 && rd && !iorq;
		_channelSelect = (Pins.CS1.IsHigh() ? 2 : 0) | (Pins.CS0.IsHigh() ? 1 : 0);

		if (!_isWrite) _writeSeen = false;

		// Latch what a read or acknowledge will drive when its strobe starts, so
		// the value stays put even if a counter decrements while it is held.
		if (_isRead) {
			if (!_readActive || _readChannel != _channelSelect) {
				_readActive = true;
				_readChannel = _channelSelect;
				_readValue = static_cast<uint8_t>(_ch[_channelSelect].count);
			}
		} else {
			_readActive = false;
		}

		if (_isAck) {
			if (!_ackActive) {
				_ackActive = true;
				const int channel = Pins.IEI.IsHigh() ? RequestingChannel() : -1;
				if (channel >= 0) {
					_ackChannel = channel;
					_ackDrive = true;
					_ackVector = static_cast<uint8_t>((_vector & 0xF8) | (channel << 1));
				}
			}
		} else {
			_ackActive = _ackDrive = false;
			_ackChannel = -1;
		}
	}

	// Highest-priority channel whose request the CPU can accept, -1 if none. A
	// channel under service blocks itself and every lower-priority channel.
	int RequestingChannel() const {
		for (int i = 0; i < 4; ++i) {
			if (_ch[i].intService) return -1;
			if (_ch[i].intPending) return i;
		}
		return -1;
	}

	void OnRisingEdge() {
		CommitAcknowledge();
		for (int i = 0; i < 4; ++i) {
			if (_ch[i].running) Tick(_ch[i]);
		}
		ObserveFetch();
		// Last, so a write takes effect from the next edge.
		if (_isWrite && !_writeSeen) {
			_writeSeen = true;
			WriteChannel(_channelSelect, Pins.D.Read<uint8_t>());
		}
	}

	void OnFallingEdge() {
		for (auto& ch : _ch)
			ch.zc = false;
	}

	void CommitAcknowledge() {
		if (_ackChannel < 0) return;
		auto& ch = _ch[_ackChannel];
		ch.intPending = false;
		ch.intService = true;
		_diagnostics.Println("{} interrupt acknowledged: channel {} vector {:02x}", _name, _ackChannel, _ackVector);
		_ackChannel = -1;
	}

	void Tick(Channel& ch) {
		if (ch.control & CtrlCounter) {
			if (ch.triggerPending) {
				ch.triggerPending = false;
				Decrement(ch);
			}
			return;
		}
		if (ch.waitTrigger) {
			if (!ch.triggerPending) return;
			ch.triggerPending = false;
			ch.waitTrigger = false;
			ch.prescaler = 0;
			ch.startDelay = true;
		}
		if (ch.startDelay) {
			ch.startDelay = false;
			return;
		}
		const uint16_t divisor = (ch.control & CtrlPrescale256) ? 256 : 16;
		if (++ch.prescaler >= divisor) {
			ch.prescaler = 0;
			Decrement(ch);
		}
	}

	void Decrement(Channel& ch) {
		if (--ch.count != 0) return;
		ch.count = TimeConstantValue(ch);
		ch.zc = true;
		if (ch.control & CtrlInterrupt) {
			ch.intPending = true;
			_diagnostics.Println("{} zero count with interrupt request", _name);
		}
	}

	void SampleTriggers(Sim::TriggerMask mask) {
		for (int i = 0; i < 4; ++i) {
			if (!mask.IsSet(ClkTrgBit(i))) continue;
			auto& ch = _ch[i];
			const bool level = Pins.ClkTrg[i].IsHigh();
			const bool activeLevel = (ch.control & CtrlRising) != 0;
			const bool edge = level != ch.triggerLevel && level == activeLevel;
			ch.triggerLevel = level;
			// An edge only matters to a channel that is counting edges or waiting to start.
			if (edge && ch.running && ((ch.control & CtrlCounter) || ch.waitTrigger)) {
				ch.triggerPending = true;
			}
		}
	}

	void WriteChannel(int index, uint8_t data) {
		auto& ch = _ch[index];
		if (ch.awaitTc) {
			LoadTimeConstant(ch, data);
			return;
		}
		if (!(data & CtrlControl)) {
			_vector = data;
			_diagnostics.Println("{} vector {:02x}", _name, data & 0xF8);
			return;
		}
		ch.control = data;
		ch.triggerPending = false;
		ch.awaitTc = (data & CtrlTcFollows) != 0;
		// Disabling interrupts withdraws a request the CPU has not acknowledged yet.
		if (!(data & CtrlInterrupt)) ch.intPending = false;
		if (data & CtrlReset) {
			ch.running = ch.waitTrigger = ch.startDelay = false;
		}
		_diagnostics.Println("{} ch{} control {:02x}", _name, index, data);
	}

	void LoadTimeConstant(Channel& ch, uint8_t data) {
		ch.awaitTc = false;
		ch.timeConstant = data;
		if (!ch.running) {
			ch.running = true;
			ch.prescaler = 0;
			ch.startDelay = false;
			ch.triggerPending = false;
			ch.waitTrigger = !(ch.control & CtrlCounter) && (ch.control & CtrlTrigger);
			ch.count = TimeConstantValue(ch);
		} else if (ch.waitTrigger) {
			ch.count = TimeConstantValue(ch);
		}
		// Otherwise the count in progress finishes first; Decrement() reloads the new constant.
		_diagnostics.Println("{} time constant {}", _name, TimeConstantValue(ch));
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
		if (_sawEd && _fetchByte == 0x4D && Pins.IEI.IsHigh()) {
			for (int i = 0; i < 4; ++i) {
				if (_ch[i].intService) {
					_ch[i].intService = false;
					_diagnostics.Println("{} RETI: channel {} service ended", _name, i);
					break;
				}
			}
		}
		_sawEd = _fetchByte == 0xED;
	}

	void DriveOutputs() {
		if (_ackDrive) Pins.D.Write(_ackVector);
		else if (_isRead) Pins.D.Write(_readValue);
		else Pins.D.Clear();

		bool busy = false;
		for (const auto& ch : _ch)
			busy |= ch.intPending || ch.intService;
		const bool iei = Pins.IEI.IsHigh();
		Pins.NotINT.Write(Sim::LogicState::TriLow(iei && RequestingChannel() >= 0));
		Pins.IEO.Write(Sim::LogicState::ActiveHigh(iei && !busy));
		for (int i = 0; i < 3; ++i) {
			Pins.ZcTo[i].Write(Sim::LogicState::ActiveHigh(_ch[i].zc));
		}
	}
};
