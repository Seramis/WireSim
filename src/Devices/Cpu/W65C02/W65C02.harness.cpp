#include "W65C02.hpp"
#include "../../../Testing/HarnessCore.hpp"

namespace {

// W65C02 has never needed a DeviceHarness shim before this: real cycle-level
// verification happens by running actual firmware through the real machine
// (`asm-test`/`mos-stepper-debugger`), not by driving its clock/address/data
// pins directly in a script -- the register state produced that way is the
// point, not something a bare pin-level script could usefully fake. This
// shim exists for exactly one thing: proving DumpRegisters() (IRegisterInspectable,
// see Inspect.hpp) actually reaches the real fields, via the only two
// registers with a deterministic value before any cycle has run -- S and P,
// set by the constructor. See W65C02.devtest and W65C02Impl.cpp's
// DumpRegisters() comment for why PC/A/X/Y aren't asserted here.
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
