#pragma once
#include "Device.hpp"
#include "LogicState.hpp"
#include <cstdint>
#include <string>

namespace Sim {

class Platform;
struct Bus;

// -----------------------------------------------------------------------------
// Pin: a device's single-wire connection point. Write() drives it, Read()
// resolves the net it's connected to. See Platform::Link to wire pins/buses
// together and overview.md for the drive-resolution and trigger semantics.
// -----------------------------------------------------------------------------
struct Pin {
	friend struct Bus;
	friend class Platform;

  public:
	static Pin New(Platform* p, std::string name = "");
	static Pin PullUp(Platform* p, std::string name = "");
	static Pin PullDown(Platform* p, std::string name = "");

	// API
	// Defined in Pin.inl.hpp (included from Platform.hpp): they need
	// Platform's complete type, not just the forward declaration above.
	void Write(LogicState val);
	LogicState Read() const;

	// Registers `device` (captured once, by its concrete type T) as a
	// trigger target for this pin's net. Defined in Pin.inl.hpp, for the
	// same reason as Write/Read above.
	template <typename T> void AddTrigger(T* device, uint8_t idBit = 0, bool isImmediate = true);

	template <typename T, typename TEnum> void AddTrigger(T* device, TEnum idBit, bool isImmediate = true) {
		AddTrigger(device, static_cast<uint8_t>(idBit), isImmediate);
	}

	inline bool IsHigh() const {
		return Read().IsHigh();
	}
	inline bool IsLow() const {
		return Read().IsLow();
	}
	inline bool IsFloating() const {
		return Read().IsFloating();
	}

	std::string ToString() const;

  private:
	Platform* _platform;
	int _id;

	Pin(Platform* p, int id) : _platform(p), _id(id) {}
};

} // namespace Sim
