#include "Z80Retro.hpp"
#include "../../Testing/MachineHarnessCore.hpp"

namespace {

// `user1=closed` fits the J9 USER1 jumper, which reads low on GP input bit 5.
Sim::Testing::MachineHandle CreateZ80Retro(const Sim::Testing::ArgsMap& args) {
	const bool user1Jumper = args.contains("user1") && args.at("user1").AsString() == "closed";

	auto platform = std::make_unique<Sim::Platform>();
	auto machine = std::make_unique<Z80Retro>(*platform, user1Jumper);

	Sim::Testing::MachineHandle h;
	h.Type = "Z80Retro";
	h.Points.emplace_back("cpu", Sim::Testing::InspectionPoint{.Registers = &machine->_cpu});
	h.Points.emplace_back("ram", Sim::Testing::InspectionPoint{.Memory = &machine->_ram});
	h.Points.emplace_back("flash", Sim::Testing::InspectionPoint{.Memory = &machine->_flash});
	h.Points.emplace_back("chipset", Sim::Testing::InspectionPoint{.Registers = &machine->_chipset});
	h.Points.emplace_back("ctc", Sim::Testing::InspectionPoint{.Registers = &machine->_ctc});
	h.Points.emplace_back("sio", Sim::Testing::InspectionPoint{.Registers = &machine->_sio});
	// A card in the ROM directory is inserted here, ahead of Boot, so that the script can address it.
	if (args.contains("rom") && machine->InsertSdCardFrom(args.at("rom").AsString())) {
		h.Points.emplace_back("sd", Sim::Testing::InspectionPoint{.Registers = &*machine->_sd, .Memory = &*machine->_sd});
	}
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
	h.Owner = std::make_unique<Sim::Testing::OwnedMachine<Z80Retro>>(std::move(machine));
	return h;
}

} // namespace

HARNESS_REGISTER_MACHINE("Z80Retro", CreateZ80Retro)
