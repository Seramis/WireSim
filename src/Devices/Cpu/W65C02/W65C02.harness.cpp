#include "W65C02.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

// Registers `PC`/`A`/`X`/`Y`/`S`/`P` through IRegisterInspectable (see
// Inspect.hpp). The CPU is driven like any other device: a script plays the
// rest of the system on CLK/D and expects A/RWB (see W65C02.md "Bus profile").
Sim::Testing::DeviceHandle CreateW65C02(Sim::Platform& p, const Sim::Testing::ArgsMap&) {
	auto dev = std::make_unique<W65C02>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("CLK", Sim::Testing::MakeStub(p, dev->CLK, "CLK"));
	h.Points.emplace_back("CLKout", Sim::Testing::MakeStub(p, dev->CLKout, "CLKout"));
	h.Points.emplace_back("IRQB", Sim::Testing::MakeStub(p, dev->IRQB, "IRQB"));
	h.Points.emplace_back("NMIB", Sim::Testing::MakeStub(p, dev->NMIB, "NMIB"));
	h.Points.emplace_back("RESB", Sim::Testing::MakeStub(p, dev->RESB, "RESB"));
	h.Points.emplace_back("RDY", Sim::Testing::MakeStub(p, dev->RDY, "RDY"));
	h.Points.emplace_back("RWB", Sim::Testing::MakeStub(p, dev->RWB, "RWB"));
	h.Points.emplace_back("A", Sim::Testing::MakeStub(p, dev->A, "A"));
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->D, "D"));
	h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<W65C02>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("W65C02", CreateW65C02)
