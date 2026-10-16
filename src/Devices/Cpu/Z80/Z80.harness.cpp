#include "../../../Testing/HarnessCore.hpp"
#include "Z80.hpp"

namespace {

// Registers through IRegisterInspectable (see Z80.md). The CPU is driven like
// any other device: a script plays the rest of the system on CLK, WAIT, INT,
// NMI, RESET, BUSREQ and D, and expects A, D and the control outputs.
Sim::Testing::DeviceHandle CreateZ80(Sim::Platform& p, const Sim::Testing::ArgsMap&) {
	auto dev = std::make_unique<Z80>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("A", Sim::Testing::MakeStub(p, dev->Pins.A, "A"));
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotM1", Sim::Testing::MakeStub(p, dev->Pins.NotM1, "NotM1"));
	h.Points.emplace_back("NotMREQ", Sim::Testing::MakeStub(p, dev->Pins.NotMREQ, "NotMREQ"));
	h.Points.emplace_back("NotIORQ", Sim::Testing::MakeStub(p, dev->Pins.NotIORQ, "NotIORQ"));
	h.Points.emplace_back("NotRD", Sim::Testing::MakeStub(p, dev->Pins.NotRD, "NotRD"));
	h.Points.emplace_back("NotWR", Sim::Testing::MakeStub(p, dev->Pins.NotWR, "NotWR"));
	h.Points.emplace_back("NotRFSH", Sim::Testing::MakeStub(p, dev->Pins.NotRFSH, "NotRFSH"));
	h.Points.emplace_back("NotHALT", Sim::Testing::MakeStub(p, dev->Pins.NotHALT, "NotHALT"));
	h.Points.emplace_back("NotWAIT", Sim::Testing::MakeStub(p, dev->Pins.NotWAIT, "NotWAIT"));
	h.Points.emplace_back("NotINT", Sim::Testing::MakeStub(p, dev->Pins.NotINT, "NotINT"));
	h.Points.emplace_back("NotNMI", Sim::Testing::MakeStub(p, dev->Pins.NotNMI, "NotNMI"));
	h.Points.emplace_back("NotRESET", Sim::Testing::MakeStub(p, dev->Pins.NotRESET, "NotRESET"));
	h.Points.emplace_back("NotBUSREQ", Sim::Testing::MakeStub(p, dev->Pins.NotBUSREQ, "NotBUSREQ"));
	h.Points.emplace_back("NotBUSACK", Sim::Testing::MakeStub(p, dev->Pins.NotBUSACK, "NotBUSACK"));
	h.Points.emplace_back("CLK", Sim::Testing::MakeStub(p, dev->Pins.CLK, "CLK"));
	h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<Z80>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("Z80", CreateZ80)
