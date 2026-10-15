#pragma once
#include "HarnessCore.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Machine-independent data model for the MachineHarness tool (see
// src/Testing/MachineHarness.md). A machine's own registration shim
// (colocated as <Machine>.machine-harness.cpp) only needs this header:
// construct the real machine, build a MachineHandle whose Points mirror its
// inspectable components, and self-register with HARNESS_REGISTER_MACHINE.
//
// Unlike DeviceHarness, there is no pin to drive directly here -- a machine
// script boots a ROM and runs real firmware through a real CPU, so state
// comes only from what that firmware does. See MachineHarness.md for why
// the run model is "tick half-cycles, count whole CPU cycles" rather than a
// `step`-per-net-settle model: Sim::Platform::Bake() resets every net's
// drive state, so (unlike a fresh DeviceHarness script) a machine script
// must stay inside exactly one Platform::Start() call for its whole run
// phase -- a second Start() call on the same Platform would silently wipe
// every currently-asserted pin (clock, reset, address/data bus) back to
// floating.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

// A named point a script can `dump`/`expect`/`poke`. Exactly one of
// Registers/Memory is set, mirroring the device-level Registers/Memory
// capability pointers on DeviceHandle (HarnessCore.hpp) -- the same pair of
// interfaces, now addressed by a point name instead of being the one
// device under test.
struct InspectionPoint {
	Sim::IRegisterInspectable* Registers = nullptr;
	Sim::IMemoryInspectable* Memory = nullptr;
};

// Type-erased ownership for a machine under test, mirroring
// IOwnedDevice/OwnedDevice<T> above -- a machine class has no reason to
// share Sim::Device's "no virtual destructor" constraint, but there's no
// reason to require one either, so this stays the same shape rather than
// introducing a second, inconsistent ownership convention.
struct IOwnedMachine {
	virtual ~IOwnedMachine() = default;
};
template <typename T> struct OwnedMachine final : IOwnedMachine {
	std::unique_ptr<T> Value;
	explicit OwnedMachine(std::unique_ptr<T> value) : Value(std::move(value)) {}
};

// What one `machine <Type> rom=<DIR> ...` line produces: the machine and the
// Sim::Platform it was built on (owned together -- a machine's wiring is
// only valid for the Platform it was linked against), its named inspection
// points, and the three verbs a script drives it with.
struct MachineHandle {
	std::string Type;
	std::unique_ptr<Sim::Platform> PlatformOwner;
	std::unique_ptr<IOwnedMachine> Owner;
	std::vector<std::pair<std::string, InspectionPoint>> Points;

	// Loads a built ROM/firmware image from `romDir` into the machine's
	// memory and performs whatever startup validation the machine's own
	// LoadRom-equivalent already does (e.g. My6502's debug-mode marker
	// check). Direct writes into a memory device's storage, not netlist
	// wiring, so this is safe to call before Platform::Start() -- the same
	// way My6502::LoadRom runs before My6502::Start launches the sim
	// thread.
	std::function<void(const std::string& romDir)> Boot;

	// Advances the simulation by exactly one half clock cycle: toggles the
	// machine's own raw clock, holds/releases reset per any pending
	// HoldReset call, and ticks diagnostics -- the same per-iteration body
	// a machine's own interactive Start() lambda already has, minus that
	// lambda's keyboard-polling loop. Called from inside the harness's one
	// Platform::Start() lambda, once per onBeforeCycle invocation. Returns
	// true the instant a full CPU clock cycle has just completed (the same
	// falling edge a machine's own cycle-limit check already keys off), so
	// the script runner can count whole CPU cycles against `run <N>`
	// without needing to know this machine's own half-cycles-per-cycle
	// ratio.
	std::function<bool()> TickHalfCycle;

	// Begins holding reset asserted for the given number of upcoming
	// TickHalfCycle() calls -- non-blocking, takes effect over the
	// following ticks rather than immediately (mirrors a machine's own
	// `_resetCycles` countdown, e.g. My6502's).
	std::function<void(int halfCycles)> HoldReset;

	InspectionPoint* Find(const std::string& name) {
		for (auto& [n, point] : Points)
			if (n == name) return &point;
		return nullptr;
	}
};

using MachineFactory = std::function<MachineHandle(const ArgsMap&)>;

// Process-wide map of machine type name -> factory, populated by each
// machine's HARNESS_REGISTER_MACHINE self-registration before main() runs.
// Same shape as DeviceRegistry (HarnessCore.hpp) -- a separate registry
// because a machine and a device are different kinds of thing with
// different handle shapes, not because the bookkeeping differs.
class MachineRegistry {
  public:
	static MachineRegistry& Instance() {
		static MachineRegistry instance;
		return instance;
	}

	void Register(std::string name, MachineFactory factory) {
		_factories[std::move(name)] = std::move(factory);
	}

	const MachineFactory* Find(const std::string& name) const {
		auto it = _factories.find(name);
		return it == _factories.end() ? nullptr : &it->second;
	}

	std::vector<std::string> Names() const {
		std::vector<std::string> names;
		for (auto& [name, factory] : _factories)
			names.push_back(name);
		return names;
	}

  private:
	std::map<std::string, MachineFactory> _factories;
};

} // namespace Sim::Testing

// Self-registers a machine factory under `typeName` before main() runs.
// Same macro shape as HARNESS_REGISTER_DEVICE (HarnessCore.hpp), reusing its
// HARNESS_CONCAT -- not semicolon-terminated at the call site.
#define HARNESS_REGISTER_MACHINE(typeName, factory)                                                                    \
	namespace {                                                                                                        \
	struct HARNESS_CONCAT(MachineHarnessRegistrar_, __LINE__) {                                                        \
		HARNESS_CONCAT(MachineHarnessRegistrar_, __LINE__)() {                                                         \
			::Sim::Testing::MachineRegistry::Instance().Register(typeName, factory);                                   \
		}                                                                                                              \
	};                                                                                                                 \
	static HARNESS_CONCAT(MachineHarnessRegistrar_, __LINE__) HARNESS_CONCAT(                                          \
		machineHarnessRegistrarInstance_, __LINE__                                                                     \
	);                                                                                                                 \
	}
