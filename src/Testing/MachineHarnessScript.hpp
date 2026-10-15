#pragma once
#include "MachineHarnessCore.hpp"
#include <iosfwd>
#include <optional>
#include <string>

// -----------------------------------------------------------------------------
// MachineHarness: owns the one machine a `.machinetest` script creates. See
// src/Testing/MachineHarness.md for the script grammar RunMachineScript
// parses and executes against this. Named distinctly from Sim::Testing::
// Harness (HarnessScript.hpp) -- a different handle shape (MachineHandle vs
// DeviceHandle), not a replacement for it.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

class MachineHarness {
  public:
	explicit MachineHarness(bool verbose) : _verbose(verbose) {}

	void CreateMachine(const std::string& type, const ArgsMap& args) {
		const MachineFactory* factory = MachineRegistry::Instance().Find(type);
		if (!factory) throw HarnessError("unknown machine type: " + type);
		MachineHandle handle = (*factory)(args);
		handle.Type = type;
		if (_verbose) handle.PlatformOwner->GetDiagnostics().debugMode = true;
		_machine.emplace(std::move(handle));
	}

	// `name` -> the named InspectionPoint on the one machine. Stable for the
	// life of the MachineHarness: the machine/its points are only ever
	// created once, during the script's setup phase.
	InspectionPoint& Resolve(const std::string& name) {
		if (!_machine) throw HarnessError("no machine created yet (script needs a 'machine <Type> rom=<DIR>' line)");
		InspectionPoint* point = _machine->Find(name);
		if (!point) throw HarnessError("machine has no inspection point named '" + name + "'");
		return *point;
	}

	MachineHandle* Machine() {
		return _machine ? &*_machine : nullptr;
	}

  private:
	bool _verbose;
	std::optional<MachineHandle> _machine;
};

struct MachineRunResult {
	int Passed = 0;
	int Failed = 0;
	int Total() const {
		return Passed + Failed;
	}
};

// Parses `source` as a `.machinetest` script and executes it against
// `harness`, writing one line per command to `out`. Throws HarnessError on
// any syntax problem or unresolvable point reference -- always before the
// machine's own Platform::Start() runs -- so a malformed script never
// partially runs.
MachineRunResult RunMachineScript(MachineHarness& harness, const std::string& source, std::ostream& out);

} // namespace Sim::Testing
