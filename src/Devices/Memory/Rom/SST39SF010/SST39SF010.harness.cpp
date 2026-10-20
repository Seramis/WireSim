#include "SST39SF010.hpp"
#include "../../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateSST39SF010(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	// `contents=@path` loads a ROM image file; `contents=<literal>` loads
	// literal bytes (mostly useful for a short inline test pattern).
	// Omitted entirely means "all 0xFF" (an erased chip).
	auto contents = args.contains("contents") ? args.at("contents").LoadBytes() : std::vector<uint8_t>{};

	auto dev = std::make_unique<SST39SF010>(&p, std::move(contents));

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("A", Sim::Testing::MakeStub(p, dev->Pins.A, "A"));
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("NotOE", Sim::Testing::MakeStub(p, dev->Pins.NotOE, "NotOE"));
	h.Points.emplace_back("NotWE", Sim::Testing::MakeStub(p, dev->Pins.NotWE, "NotWE"));
	h.Memory = dev.get();
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<SST39SF010>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("SST39SF010", CreateSST39SF010)
