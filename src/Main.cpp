// Entry point shared by every machine executable. CMake's
// wiresim_add_machine() compiles this file once per machine, defining:
//   MACHINE_HEADER           the machine's header, e.g. "Machines/My6502/My6502.hpp"
//   MACHINE_CLASS            the machine class (also used as the ROM directory name)
//   MACHINE_NAME             the same, as a string literal
//   MACHINE_DEFAULT_VARIANT  firmware variant used when --variant/--rom are absent
// (one machine per translation unit: the machine headers don't share include guards).
#include MACHINE_HEADER
#include "Simulator/Platform.hpp"
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

int Usage() {
	std::cerr << "Usage: " MACHINE_NAME " [--cycles COUNT] [--variant NAME | --rom DIR]\n"
			  << "  --cycles COUNT   stop after COUNT CPU cycles (default: run until quit)\n"
			  << "  --variant NAME   firmware variant built by CMake, from <exe dir>/rom/" MACHINE_NAME "-NAME (default: "
			  << MACHINE_DEFAULT_VARIANT << ")\n"
			  << "  --rom DIR        load ROM images from DIR instead\n";
	return 2;
}

} // namespace

int main(int argc, char** argv) {
	std::optional<uint64_t> cycleLimit;
	std::optional<std::string> variant;
	std::optional<std::string> romDir;
	for (int i = 1; i < argc; ++i) {
		std::string_view arg(argv[i]);
		if (arg == "--cycles" && i + 1 < argc && !cycleLimit) {
			uint64_t count = 0;
			std::string_view value(argv[++i]);
			auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
			if (error != std::errc{} || end != value.data() + value.size() || count == 0) {
				std::cerr << "--cycles requires a positive integer" << std::endl;
				return 2;
			}
			cycleLimit = count;
		} else if (arg == "--variant" && i + 1 < argc && !variant) {
			variant = argv[++i];
		} else if (arg == "--rom" && i + 1 < argc && !romDir) {
			romDir = argv[++i];
		} else {
			return Usage();
		}
	}
	if (variant && romDir) return Usage();

	if (!romDir) {
		// Firmware variants are built next to the executable (build/<preset>/rom/).
		const std::filesystem::path exeDir = std::filesystem::read_symlink("/proc/self/exe").parent_path();
		romDir = (exeDir / "rom" / (MACHINE_NAME "-" + variant.value_or(MACHINE_DEFAULT_VARIANT))).string();
	}

	Sim::Platform p;

	MACHINE_CLASS machine(p);

	try {
		machine.LoadRom(*romDir);
	} catch (const std::exception& e) {
		std::cerr << MACHINE_NAME ": " << e.what() << std::endl;
		return 1;
	}
	machine.Start(cycleLimit);
	return 0;
}
