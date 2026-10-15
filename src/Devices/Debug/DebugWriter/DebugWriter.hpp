#pragma once
#include "../../../Simulator/Platform.hpp"
#include <cstdint>
#include <fstream>
#include <optional>
#include <print>
#include <string>
#include <utility>

// Write-only debug sink: this device never drives D, so there's no output
// enable pin, only a data bus and the chip-select/write-strobe pair needed to
// capture a write. Each byte written either gets appended to a file
// (truncated once up front so every run starts clean) or printed to stdout
// with a recognizable prefix. That print IS the device's output, not an
// internal trace, so it is deliberately not gated on Diagnostics::debugMode.
class DebugWriter final : public Sim::Device {
  public:
	struct DebugWriterPins {
		Sim::Bus D;
		Sim::Pin NotCE, NotWE;
	};
	DebugWriterPins Pins;

  private:
	enum class CTRL : uint8_t {
		NotCE = 0,
		NotWE = 1
	};

	std::string _name;
	std::ofstream _file;

  public:
	DebugWriter(
		Sim::Platform* p, std::string name = "DebugWriter", std::optional<std::string> filePath = std::nullopt
	) :
		Pins{Sim::Bus::New(p, 8, name + ".D"), Sim::Pin::New(p, name + ".NotCE"), Sim::Pin::New(p, name + ".NotWE")},
		_name(std::move(name)) {
		if (filePath) {
			_file.open(*filePath, std::ios::out | std::ios::trunc | std::ios::binary);
		}

		Pins.NotCE.AddTrigger(this, CTRL::NotCE);
		Pins.NotWE.AddTrigger(this, CTRL::NotWE, false);
	}

	void Logic(Sim::TriggerMask mask) {
		// Act only when NotWE's own (stable) trigger fires, not on every
		// immediate settle iteration where isWrite happens to already be
		// true -- otherwise the same byte would be emitted multiple times
		// per cycle while the settle loop converges. Checked before the
		// pin reads below since it's cheaper than even one of them.
		if (!mask.IsSet(CTRL::NotWE)) return;

		if (Pins.NotCE.IsLow() && Pins.NotWE.IsLow()) {
			const auto value = Pins.D.Read<uint8_t>();
			if (_file.is_open()) {
				_file.put(static_cast<char>(value));
				_file.flush();
			} else {
				std::println("[DebugWriter:{}] write {:02x}", _name, value);
			}
		}
	}
};
