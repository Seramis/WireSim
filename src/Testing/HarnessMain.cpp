#include "HarnessCore.hpp"
#include "HarnessScript.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

// Usage: DeviceHarness [-v] <script.devtest>
//        DeviceHarness [-v] -        (read the script from stdin)
//        DeviceHarness --list-devices
// See src/Testing/DeviceHarness.md for the script grammar.
int main(int argc, char** argv) {
	// A `click`/`screenshot`-using script opens a real SDL window by
	// convention (see a UI device's own <Name>.harness.cpp -- it calls
	// OpenWindow() itself, no script verb needed). Defaulting to the
	// dummy video driver means that window is never actually visible and,
	// just as importantly, never races a real compositor's first-frame
	// buffer handshake (see SdlUiTestHooks.md's "Why MinFramesAfterOpen"
	// for the Wayland bug this sidesteps). The trailing `0` means "don't
	// overwrite" -- `SDL_VIDEODRIVER=x11 DeviceHarness -v script.devtest`
	// still works for watching a script live.
	setenv("SDL_VIDEODRIVER", "dummy", 0);

	bool verbose = false;
	std::string scriptPath;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "-v" || arg == "--diag") verbose = true;
		else if (arg == "--list-devices") {
			for (auto& name : Sim::Testing::DeviceRegistry::Instance().Names())
				std::cout << name << "\n";
			return 0;
		} else if (scriptPath.empty()) {
			scriptPath = arg;
		} else {
			std::cerr << "Usage: DeviceHarness [-v] <script.devtest>\n";
			return 2;
		}
	}

	if (scriptPath.empty()) {
		std::cerr << "Usage: DeviceHarness [-v] <script.devtest>\n";
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

	Sim::Testing::Harness harness(verbose);
	try {
		const Sim::Testing::RunResult result = Sim::Testing::RunScript(harness, source.str(), std::cout);
		return result.Failed == 0 ? 0 : 1;
	} catch (const Sim::Testing::HarnessError& e) {
		std::cerr << "error: " << e.what() << "\n";
		return 2;
	}
}
