#include "ST16C550.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

// ST16C550 has no external transport argument of its own -- it mixes in
// Terminal, which spawns its own TX/RX worker threads (see Terminal.hpp) --
// so no stub/null transport is needed here. A script that never connects its
// RX/TX stub pins to anything will simply never see a byte arrive or leave;
// that's a realistic "transport not wired up" state, not a harness gap.
Sim::Testing::DeviceHandle CreateST16C550(Sim::Platform& p, const Sim::Testing::ArgsMap&) {
	auto dev = std::make_unique<ST16C550>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("A", Sim::Testing::MakeStub(p, dev->Pins.A, "A"));
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotIOR", Sim::Testing::MakeStub(p, dev->Pins.NotIOR, "NotIOR"));
	h.Points.emplace_back("IOR", Sim::Testing::MakeStub(p, dev->Pins.IOR, "IOR"));
	h.Points.emplace_back("NotIOW", Sim::Testing::MakeStub(p, dev->Pins.NotIOW, "NotIOW"));
	h.Points.emplace_back("IOW", Sim::Testing::MakeStub(p, dev->Pins.IOW, "IOW"));
	h.Points.emplace_back("CS0", Sim::Testing::MakeStub(p, dev->Pins.CS0, "CS0"));
	h.Points.emplace_back("CS1", Sim::Testing::MakeStub(p, dev->Pins.CS1, "CS1"));
	h.Points.emplace_back("NotCS2", Sim::Testing::MakeStub(p, dev->Pins.NotCS2, "NotCS2"));
	h.Points.emplace_back("INT", Sim::Testing::MakeStub(p, dev->Pins.INT, "INT"));
	h.Points.emplace_back("RESET", Sim::Testing::MakeStub(p, dev->Pins.RESET, "RESET"));
	h.Points.emplace_back("RX", Sim::Testing::MakeStub(p, dev->Pins.RX, "RX"));
	h.Points.emplace_back("TX", Sim::Testing::MakeStub(p, dev->Pins.TX, "TX"));
	h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<ST16C550>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("ST16C550", CreateST16C550)
