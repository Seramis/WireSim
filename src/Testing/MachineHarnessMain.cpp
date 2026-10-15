#include "MachineHarnessCore.hpp"
#include "MachineHarnessScript.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

// Usage: MachineHarness [-v] <script.machinetest>
//        MachineHarness [-v] -        (read the script from stdin)
//        MachineHarness --list-machines
// See src/Testing/MachineHarness.md for the script grammar.
int main(int argc, char** argv) {
	// Same reasoning as DeviceHarness (HarnessMain.cpp): a machine that
	// opens a windowed peripheral shouldn't need a real display to run
	// headless. The trailing `0` means "don't overwrite" -- an invoker can
	// still set SDL_VIDEODRIVER explicitly to watch a windowed peripheral
	// live.
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	bool verbose = false;
	std::string scriptPath;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-v" || arg == "--diag") verbose = true;
		else if (arg == "--list-machines") {
			for (auto& name : Sim::Testing::MachineRegistry::Instance().Names())
				std::cout << name << "\n";
			return 0;
		} else if (scriptPath.empty()) {
			scriptPath = arg;
		} else {
			std::cerr << "Usage: MachineHarness [-v] <script.machinetest>\n";
			return 2;
		}
	}

	if (scriptPath.empty()) {
		std::cerr << "Usage: MachineHarness [-v] <script.machinetest>\n";
		return 2;
	}

	std::stringstream source;
	if (scriptPath == "-") {
		source << std::cin.rdbuf();
	} else {
		std::ifstream file(scriptPath);
		if (!file) {
			std::cerr << "cannot open script: " << scriptPath << "\n";
			return 2;
		}
		source << file.rdbuf();
	}

	Sim::Testing::MachineHarness harness(verbose);
	try {
		const Sim::Testing::MachineRunResult result = Sim::Testing::RunMachineScript(harness, source.str(), std::cout);
		return result.Failed == 0 ? 0 : 1;
	} catch (const Sim::Testing::HarnessError& e) {
		std::cerr << "error: " << e.what() << "\n";
		return 2;
	}
}
