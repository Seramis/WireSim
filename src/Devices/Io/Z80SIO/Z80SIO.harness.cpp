#include "Z80SIO.hpp"
#include "../../../Testing/HarnessCore.hpp"
#include "../../Serial/TcpTerminal.hpp"

namespace {

void AddChannel(Sim::Testing::DeviceHandle& h, Sim::Platform& p, Z80SIO::ChannelPins& pins, const std::string& ch) {
	h.Points.emplace_back("RxD" + ch, Sim::Testing::MakeStub(p, pins.RxD, "RxD" + ch));
	h.Points.emplace_back("TxD" + ch, Sim::Testing::MakeStub(p, pins.TxD, "TxD" + ch));
	h.Points.emplace_back("RxC" + ch, Sim::Testing::MakeStub(p, pins.RxC, "RxC" + ch));
	h.Points.emplace_back("TxC" + ch, Sim::Testing::MakeStub(p, pins.TxC, "TxC" + ch));
	h.Points.emplace_back("NotCTS" + ch, Sim::Testing::MakeStub(p, pins.NotCTS, "NotCTS" + ch));
	h.Points.emplace_back("NotDCD" + ch, Sim::Testing::MakeStub(p, pins.NotDCD, "NotDCD" + ch));
	h.Points.emplace_back("NotSYNC" + ch, Sim::Testing::MakeStub(p, pins.NotSYNC, "NotSYNC" + ch));
	h.Points.emplace_back("NotRTS" + ch, Sim::Testing::MakeStub(p, pins.NotRTS, "NotRTS" + ch));
	h.Points.emplace_back("NotDTR" + ch, Sim::Testing::MakeStub(p, pins.NotDTR, "NotDTR" + ch));
	h.Points.emplace_back("WRdy" + ch, Sim::Testing::MakeStub(p, pins.WRdy, "WRdy" + ch));
}

// Echoes whatever it receives, through the threaded Terminal path (null-modem linked to channel A for
// `link=echo`). Only for ad-hoc `sleep`-based checks.
class EchoPeer final : public Terminal {
  protected:
	void Receive(uint8_t b) override {
		Send(b);
	}
};

struct SioWithPeer final : Sim::Testing::IOwnedDevice {
	std::unique_ptr<Z80SIO> Dev;
	std::unique_ptr<Terminal> Peer;
};

Sim::Testing::DeviceHandle CreateZ80SIO(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	auto dev = std::make_unique<Z80SIO>(&p);

	// `link=loopback`: each channel is byte-linked and its transmitted characters come back as its own
	// received ones, synchronously, so no sleeps are needed. `link=echo`: channel A is null-modem
	// linked to an echoing Terminal, through the threaded path. `link=tcp port=N`: channel A is
	// null-modem linked to a TcpTerminal listening on port N. Both are for ad-hoc `sleep` checks.
	std::unique_ptr<Terminal> peer;
	if (const auto it = args.find("link"); it != args.end()) {
		const std::string& mode = it->second.AsString();
		Z80SIO* d = dev.get();
		if (mode == "loopback") {
			for (int ch = 0; ch < 2; ++ch)
				d->BindByteLink(ch, {[d, ch](uint8_t b) { d->HostReceive(ch, b); }, nullptr});
		} else if (mode == "echo") {
			peer = std::make_unique<EchoPeer>();
			Terminal::NullModemConnect(peer.get(), &dev->HostTerminal(0));
		} else if (mode == "tcp") {
			auto tcp = std::make_unique<TcpTerminal>("Z80SIO", args.at("port").AsInt());
			Terminal::NullModemConnect(tcp.get(), &dev->HostTerminal(0));
			tcp->Start();
			peer = std::move(tcp);
		} else {
			throw Sim::Testing::HarnessError("Z80SIO: unknown link=" + mode);
		}
	}

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("BNotA", Sim::Testing::MakeStub(p, dev->Pins.BNotA, "BNotA"));
	h.Points.emplace_back("CNotD", Sim::Testing::MakeStub(p, dev->Pins.CNotD, "CNotD"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("CLK", Sim::Testing::MakeStub(p, dev->Pins.CLK, "CLK"));
	h.Points.emplace_back("NotM1", Sim::Testing::MakeStub(p, dev->Pins.NotM1, "NotM1"));
	h.Points.emplace_back("NotIORQ", Sim::Testing::MakeStub(p, dev->Pins.NotIORQ, "NotIORQ"));
	h.Points.emplace_back("NotRD", Sim::Testing::MakeStub(p, dev->Pins.NotRD, "NotRD"));
	h.Points.emplace_back("NotRESET", Sim::Testing::MakeStub(p, dev->Pins.NotRESET, "NotRESET"));
	h.Points.emplace_back("IEI", Sim::Testing::MakeStub(p, dev->Pins.IEI, "IEI"));
	h.Points.emplace_back("IEO", Sim::Testing::MakeStub(p, dev->Pins.IEO, "IEO"));
	h.Points.emplace_back("NotINT", Sim::Testing::MakeStub(p, dev->Pins.NotINT, "NotINT"));
	AddChannel(h, p, dev->Pins.A, "A");
	AddChannel(h, p, dev->Pins.B, "B");
	h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
	if (peer) {
		auto owner = std::make_unique<SioWithPeer>();
		owner->Dev = std::move(dev);
		owner->Peer = std::move(peer);
		h.Owner = std::move(owner);
	} else {
		h.Owner = std::make_unique<Sim::Testing::OwnedDevice<Z80SIO>>(std::move(dev));
	}
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("Z80SIO", CreateZ80SIO)
