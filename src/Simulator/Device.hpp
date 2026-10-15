#pragma once
#include <cstdint>

namespace Sim {

// -----------------------------------------------------------------------------
// TriggerMask
// -----------------------------------------------------------------------------
struct TriggerMask {
	uint64_t Value;

	constexpr TriggerMask(uint64_t v = 0) : Value(v) {}

	constexpr bool IsSet(uint8_t bitIndex) const {
		return (Value & (1ULL << bitIndex)) != 0;
	}

	template <typename TEnum> constexpr bool IsSet(TEnum bit) const {
		return IsSet(static_cast<uint8_t>(bit));
	}

	// Combines bits into a single OR'd mask; with compile-time-constant
	// arguments (the only way this is ever called) this collapses to one
	// immediate, so IsSetAny/IsSetAll cost one AND regardless of bit count.
	template <typename... Bits> static constexpr uint64_t Mask(Bits... bits) {
		return ((1ULL << static_cast<uint8_t>(bits)) | ... | 0ULL);
	}

	template <typename... Bits> constexpr bool IsSetAny(Bits... bits) const {
		return (Value & Mask(bits...)) != 0;
	}

	template <typename... Bits> constexpr bool IsSetAll(Bits... bits) const {
		const uint64_t m = Mask(bits...);
		return (Value & m) == m;
	}

	explicit operator uint64_t() const {
		return Value;
	}
};

// -----------------------------------------------------------------------------
// Device: base identity for a netlist participant. Logic() is deliberately
// NOT virtual -- Platform dispatches it through a type-erased function
// pointer captured once at registration time (see Pin::AddTrigger's
// template, defined in Platform.hpp), so the hot dispatch loop avoids the
// vtable indirection (and the vptr it would add to every device object).
// A device class implements `void Logic(Sim::TriggerMask mask)` by
// convention; it is NOT declared or enforced here, only checked (via a
// `requires` clause) at the point a device first calls AddTrigger.
// -----------------------------------------------------------------------------
class Device {
	friend class Platform;

  private:
	int _deviceId = -1; // Assigned by Platform upon registration
  public:
	using LogicFn = void (*)(Device*, TriggerMask);

	inline int DeviceId() const {
		return _deviceId;
	}
};

} // namespace Sim
