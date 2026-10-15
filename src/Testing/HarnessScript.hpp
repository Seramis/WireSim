#pragma once
#include "HarnessCore.hpp"
#include <iosfwd>
#include <optional>
#include <string>

// -----------------------------------------------------------------------------
// Harness: owns the Sim::Platform for one script run, plus the single device
// it creates -- the harness is deliberately single-device only (see
// src/Testing/DeviceHarness.md's "Why single-device"). See that doc for the
// script grammar RunScript parses and executes against this.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

class Harness {
  public:
	explicit Harness(bool verbose) {
		_platform.GetDiagnostics().debugMode = verbose;
	}

	void CreateDevice(const std::string& type, const ArgsMap& args) {
		const DeviceFactory* factory = DeviceRegistry::Instance().Find(type);
		if (!factory) throw HarnessError("unknown device type: " + type);
		DeviceHandle handle = (*factory)(_platform, args);
		handle.Type = type;
		_device.emplace(std::move(handle));
	}

	// `pin` -> the stub TestPoint of that name on the one device. Stable for
	// the life of the Harness: the device/its points are only ever created
	// once, during the script's setup phase, never during a caller's own
	// lifetime.
	TestPoint& Resolve(const std::string& pin) {
		if (!_device) throw HarnessError("no device created yet (script needs a 'device <Type>' line)");
		TestPoint* point = _device->Find(pin);
		if (!point) throw HarnessError("device has no pin named '" + pin + "'");
		return *point;
	}

	DeviceHandle* Device() {
		return _device ? &*_device : nullptr;
	}

	Sim::Platform& PlatformRef() {
		return _platform;
	}

  private:
	Sim::Platform _platform;
	std::optional<DeviceHandle> _device;
};

struct RunResult {
	int Passed = 0;
	int Failed = 0;
	int Total() const {
		return Passed + Failed;
	}
};

// Parses `source` as a harness script and executes it against `harness`,
// writing one line per command to `out`. Throws HarnessError on any syntax
// problem or unresolvable pin reference -- always before any device is
// stepped -- so a malformed script never partially runs.
RunResult RunScript(Harness& harness, const std::string& source, std::ostream& out);

} // namespace Sim::Testing
