#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// -----------------------------------------------------------------------------
// Optional, opt-in inspection interfaces. A device implements one of these
// only if it wants its state readable by DeviceHarness/MachineHarness after
// the sim is stopped. They're never called from Logic() or any other hot
// path, so ordinary virtual dispatch is fine here even though Device::Logic
// itself avoids a vtable (see Device.hpp). They live here rather than on
// Sim::Diagnostics -- which stays counters-only, since it's threaded through
// every device's hot Logic() call -- so a device that implements neither
// interface pays nothing for them.
// -----------------------------------------------------------------------------
namespace Sim {

class IRegisterInspectable {
  public:
	virtual ~IRegisterInspectable() = default;

	// Name -> value, in the device's own chosen order (conventionally the
	// same order an existing DumpState()-style method already prints, so
	// the two don't drift against each other).
	virtual std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const = 0;
};

class IMemoryInspectable {
  public:
	virtual ~IMemoryInspectable() = default;

	virtual uint32_t InspectableSize() const = 0;
	virtual uint8_t Peek(uint32_t address) const = 0;
	// Test/setup only -- seeding memory before a run, not something
	// production wiring ever calls.
	virtual void Poke(uint32_t address, uint8_t value) = 0;
};

} // namespace Sim
