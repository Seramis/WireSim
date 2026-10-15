#include "W65C51N.hpp"
#include "../../../Testing/HarnessCore.hpp"
#include <atomic>
#include <memory>
#include <vector>

namespace {

// The terminal on the other end of the serial line, so a script can exercise the
// transport side that no pin reaches: bytes arriving at the ACIA, bytes it
// transmits, and RTS/CTS in both directions.
class HostPeer final : public Terminal {
  public:
	std::atomic<int> TxBytes{0};	  // bytes the ACIA transmitted to us
	std::atomic<int> TxLast{0};		  // the most recent one
	std::atomic<int> AciaRtsReady{0}; // the ACIA's last announced RTS: 1 = clear to send to it

	void Feed(uint8_t b) {
		Send(b);
	}
	void Announce(bool ready) {
		UpdateRTS(ready);
	}

  protected:
	void Receive(uint8_t b) override {
		TxLast = b;
		++TxBytes;
	}
	void CTS(bool clearToSend) override {
		AciaRtsReady = clearToSend ? 1 : 0;
	}
};

// Script-visible side of HostPeer: a rising edge on HostSend feeds the next byte of `rx=` to
// the ACIA; HostRTS is the peer's own RTS (driven low = not ready; idle counts as ready), announced on every change.
class HostLine final : public Sim::Device {
  public:
	struct HostLinePins {
		Sim::Pin HostSend, HostRTS;
	};
	HostLinePins Pins;

	HostLine(Sim::Platform* p, HostPeer& peer, std::vector<uint8_t> rx) :
		Pins{Sim::Pin::New(p, "HostSend"), Sim::Pin::New(p, "HostRTS")}, _peer(peer), _rx(std::move(rx)) {
		Pins.HostSend.AddTrigger(this, Trig::Send);
		Pins.HostRTS.AddTrigger(this, Trig::Rts);
	}

	void Logic(Sim::TriggerMask mask) {
		if (mask.IsSet(Trig::Rts)) _peer.Announce(!Pins.HostRTS.IsLow());
		if (mask.IsSet(Trig::Send)) {
			const bool high = Pins.HostSend.IsHigh();
			if (high && !_lastSend && _next < _rx.size()) _peer.Feed(_rx[_next++]);
			_lastSend = high;
		}
	}

  private:
	enum class Trig : uint8_t {
		Send = 0,
		Rts = 1
	};
	HostPeer& _peer;
	std::vector<uint8_t> _rx;
	size_t _next = 0;
	bool _lastSend = false;
};

// Read-only view of the peer's counters for `expect reg`.
class PeerProbe final : public Sim::IRegisterInspectable {
  public:
	explicit PeerProbe(HostPeer& peer) : _peer(peer) {}
	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return {
			{"TxBytes", static_cast<uint64_t>(_peer.TxBytes.load())},
			{"TxLast", static_cast<uint64_t>(_peer.TxLast.load())},
			{"AciaRtsReady", static_cast<uint64_t>(_peer.AciaRtsReady.load())},
		};
	}

  private:
	HostPeer& _peer;
};

// Declaration order is destruction order in reverse: the ACIA goes before the peer it is wired to.
struct W65C51NRig final : Sim::Testing::IOwnedDevice {
	HostPeer peer;
	std::unique_ptr<W65C51N> acia;
	std::unique_ptr<HostLine> line;
	PeerProbe probe;

	W65C51NRig(Sim::Platform& p, std::vector<uint8_t> rx) :
		acia(std::make_unique<W65C51N>(&p)), line(std::make_unique<HostLine>(&p, peer, std::move(rx))), probe(peer) {
		Terminal::NullModemConnect(&peer, acia.get());
	}
};

// `rx=<bytes>` (literal, or @file) is the byte string a script feeds the ACIA, one byte per rising edge on HostSend.
Sim::Testing::DeviceHandle CreateW65C51N(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	std::vector<uint8_t> rx;
	if (auto it = args.find("rx"); it != args.end()) rx = it->second.LoadBytes();
	auto rig = std::make_unique<W65C51NRig>(p, std::move(rx));
	W65C51N& dev = *rig->acia;

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev.Pins.D, "D"));
	h.Points.emplace_back("RS0", Sim::Testing::MakeStub(p, dev.Pins.RS0, "RS0"));
	h.Points.emplace_back("RS1", Sim::Testing::MakeStub(p, dev.Pins.RS1, "RS1"));
	h.Points.emplace_back("RWB", Sim::Testing::MakeStub(p, dev.Pins.RWB, "RWB"));
	h.Points.emplace_back("CS0", Sim::Testing::MakeStub(p, dev.Pins.CS0, "CS0"));
	h.Points.emplace_back("CS1B", Sim::Testing::MakeStub(p, dev.Pins.CS1B, "CS1B"));
	h.Points.emplace_back("RESB", Sim::Testing::MakeStub(p, dev.Pins.RESB, "RESB"));
	h.Points.emplace_back("PHI2", Sim::Testing::MakeStub(p, dev.Pins.PHI2, "PHI2"));
	h.Points.emplace_back("IRQB", Sim::Testing::MakeStub(p, dev.Pins.IRQB, "IRQB"));
	h.Points.emplace_back("RTSB", Sim::Testing::MakeStub(p, dev.Pins.RTSB, "RTSB"));
	h.Points.emplace_back("CTSB", Sim::Testing::MakeStub(p, dev.Pins.CTSB, "CTSB"));
	h.Points.emplace_back("DTRB", Sim::Testing::MakeStub(p, dev.Pins.DTRB, "DTRB"));
	h.Points.emplace_back("DSRB", Sim::Testing::MakeStub(p, dev.Pins.DSRB, "DSRB"));
	h.Points.emplace_back("DCDB", Sim::Testing::MakeStub(p, dev.Pins.DCDB, "DCDB"));
	h.Points.emplace_back("TxD", Sim::Testing::MakeStub(p, dev.Pins.TxD, "TxD"));
	h.Points.emplace_back("RxD", Sim::Testing::MakeStub(p, dev.Pins.RxD, "RxD"));
	h.Points.emplace_back("RxC", Sim::Testing::MakeStub(p, dev.Pins.RxC, "RxC"));
	h.Points.emplace_back("XTLI", Sim::Testing::MakeStub(p, dev.Pins.XTLI, "XTLI"));
	h.Points.emplace_back("XTLO", Sim::Testing::MakeStub(p, dev.Pins.XTLO, "XTLO"));
	h.Points.emplace_back("HostSend", Sim::Testing::MakeStub(p, rig->line->Pins.HostSend, "HostSend"));
	h.Points.emplace_back("HostRTS", Sim::Testing::MakeStub(p, rig->line->Pins.HostRTS, "HostRTS"));
	h.Registers = &rig->probe;
	h.Owner = std::move(rig);
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("W65C51N", CreateW65C51N)
