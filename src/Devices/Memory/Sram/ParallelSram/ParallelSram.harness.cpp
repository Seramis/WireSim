#include "ParallelSram.hpp"
#include "../../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateParallelSram(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	const int addrWidth = args.contains("addrWidth") ? args.at("addrWidth").AsInt() : 11;
	const int dataWidth = args.contains("dataWidth") ? args.at("dataWidth").AsInt() : 8;

	auto dev = std::make_unique<ParallelSram>(&p, addrWidth, dataWidth);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("A", Sim::Testing::MakeStub(p, dev->Pins.A, "A"));
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("NotWE", Sim::Testing::MakeStub(p, dev->Pins.NotWE, "NotWE"));
	h.Points.emplace_back("NotOE", Sim::Testing::MakeStub(p, dev->Pins.NotOE, "NotOE"));
	h.Memory = dynamic_cast<Sim::IMemoryInspectable*>(dev.get());
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<ParallelSram>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("ParallelSram", CreateParallelSram)
