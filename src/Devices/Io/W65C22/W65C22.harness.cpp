#include "W65C22.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

Sim::Testing::DeviceHandle CreateW65C22(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	auto it = args.find("clock");
	const bool internalThread = it != args.end() && it->second.AsString() == "internal";
	const double hz = args.count("hz") ? (double)args.at("hz").AsInt() : 1'000'000.0;
	auto variantIt = args.find("variant");
	const bool variantN = variantIt != args.end() && variantIt->second.AsString() == "n";

	auto dev = std::make_unique<W65C22>(
		&p,
		variantN ? W65C22::Variant::N : W65C22::Variant::S,
		internalThread ? W65C22::TimerClockSource::InternalThread : W65C22::TimerClockSource::ClkPin,
		hz
	);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("PA", Sim::Testing::MakeStub(p, dev->Pins.PA, "PA"));
	h.Points.emplace_back("PB", Sim::Testing::MakeStub(p, dev->Pins.PB, "PB"));
	h.Points.emplace_back("RS", Sim::Testing::MakeStub(p, dev->Pins.RS, "RS"));
	h.Points.emplace_back("CS1", Sim::Testing::MakeStub(p, dev->Pins.CS1, "CS1"));
	h.Points.emplace_back("CS2B", Sim::Testing::MakeStub(p, dev->Pins.CS2B, "CS2B"));
	h.Points.emplace_back("RWB", Sim::Testing::MakeStub(p, dev->Pins.RWB, "RWB"));
	h.Points.emplace_back("PHI2", Sim::Testing::MakeStub(p, dev->Pins.PHI2, "PHI2"));
	h.Points.emplace_back("RESB", Sim::Testing::MakeStub(p, dev->Pins.RESB, "RESB"));
	h.Points.emplace_back("IRQB", Sim::Testing::MakeStub(p, dev->Pins.IRQB, "IRQB"));
	h.Points.emplace_back("CA1", Sim::Testing::MakeStub(p, dev->Pins.CA1, "CA1"));
	h.Points.emplace_back("CA2", Sim::Testing::MakeStub(p, dev->Pins.CA2, "CA2"));
	h.Points.emplace_back("CB1", Sim::Testing::MakeStub(p, dev->Pins.CB1, "CB1"));
	h.Points.emplace_back("CB2", Sim::Testing::MakeStub(p, dev->Pins.CB2, "CB2"));
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<W65C22>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("W65C22", CreateW65C22)
