#include "Register8.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateRegister8(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	const std::string name = args.contains("name") ? args.at("name").AsString() : "Register8";
	auto dev = std::make_unique<Register8>(&p, name);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	h.Points.emplace_back("NotWE", Sim::Testing::MakeStub(p, dev->Pins.NotWE, "NotWE"));
	h.Points.emplace_back("NotOE", Sim::Testing::MakeStub(p, dev->Pins.NotOE, "NotOE"));
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<Register8>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("Register8", CreateRegister8)
