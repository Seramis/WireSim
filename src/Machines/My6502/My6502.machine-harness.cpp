#include "My6502.hpp"
#include "../../Testing/MachineHarnessCore.hpp"

namespace {

Sim::Testing::MachineHandle CreateMy6502(const Sim::Testing::ArgsMap&) {
	auto platform = std::make_unique<Sim::Platform>();
	auto machine = std::make_unique<My6502>(*platform);

	Sim::Testing::MachineHandle h;
	h.Type = "My6502";
	h.Points.emplace_back("cpu", Sim::Testing::InspectionPoint{.Registers = &machine->_cpu});
	h.Points.emplace_back("ram", Sim::Testing::InspectionPoint{.Memory = &machine->_ram});
	h.Points.emplace_back("uart", Sim::Testing::InspectionPoint{.Registers = &machine->_uart});
	h.Boot = [m = machine.get()](const std::string& romDir) {
		m->LoadRom(romDir);
	};
	h.TickHalfCycle = [m = machine.get()] {
		return m->TickHalfCycle();
	};
	h.HoldReset = [m = machine.get()](int halfCycles) {
		m->HoldReset(halfCycles);
	};
	h.PlatformOwner = std::move(platform);
	h.Owner = std::make_unique<Sim::Testing::OwnedMachine<My6502>>(std::move(machine));
	return h;
}

} // namespace

HARNESS_REGISTER_MACHINE("My6502", CreateMy6502)
