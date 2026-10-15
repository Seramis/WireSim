#include "BusTerminal.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

// BusTerminal has no external transport argument of its own -- it mixes in
// Terminal, which spawns its own TX/RX worker threads (see Terminal.hpp) --
// so no stub/null transport is needed here. A script that never connects
// it to a peer Terminal will simply never see a byte arrive or leave; see
// BusTerminal.md's Test coverage for why DeviceHarness can't exercise that
// path directly.
Sim::Testing::DeviceHandle CreateBusTerminal(Sim::Platform& p, const Sim::Testing::ArgsMap&) {
	auto dev = std::make_unique<BusTerminal>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("NotOE", Sim::Testing::MakeStub(p, dev->Pins.NotOE, "NotOE"));
	h.Points.emplace_back("NotWE", Sim::Testing::MakeStub(p, dev->Pins.NotWE, "NotWE"));
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<BusTerminal>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("BusTerminal", CreateBusTerminal)
