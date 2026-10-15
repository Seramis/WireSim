#pragma once
#include "Pin.hpp"
#include "src/Simulator/Platform.hpp"

// -----------------------------------------------------------------------------
// Pin::AddTrigger/Write/Read need Platform's complete type (they reach into
// its private storage), which isn't available in Pin.hpp -- Platform is only
// forward-declared there. This file holds those definitions so they stay
// next to Pin's declaration instead of buried in Platform.hpp; it relies on
// Platform already being fully defined at the point it's included, so only
// Platform.hpp includes it, after its own class body.
// -----------------------------------------------------------------------------
namespace Sim {

// T is deduced from the device's own `this` at the call site (inside that
// device's constructor), so it is still a concrete, known type here --
// unlike Device*, which has already lost it. That lets us build a thunk
// that calls T::Logic directly (no vtable) and stash it, once per device
// instance, in Platform's flat LogicFns table.
template <typename T> inline void Pin::AddTrigger(T* device, uint8_t idBit, bool isImmediate) {
	static_assert(std::is_base_of_v<Device, T>, "AddTrigger requires a type derived from Sim::Device");
	static_assert(
		requires(T& t, TriggerMask m) { t.Logic(m); }, "Device types must implement void Logic(Sim::TriggerMask)"
	);

	Device* base = device;
	if (base->DeviceId() == -1) {
		_platform->RegisterDevice(
			base, +[](Device* d, TriggerMask mask) {
				static_cast<T*>(d)->Logic(mask);
			}
		);
	}

	int netId = _platform->_pins.NetIds[_id];
	int trigId = _platform->_triggers.NewId(base->DeviceId(), idBit);

	if (isImmediate) _platform->_nets.ImmediateTriggerIds[netId].push_back(trigId);
	else _platform->_nets.StableTriggerIds[netId].push_back(trigId);
}

// Defined here (rather than Pin.cpp) so every including TU can inline these
// at their call sites -- scattered across every device's Logic() -- without
// depending on cross-TU LTO inlining for functions this small and this
// widely called.
inline void Pin::Write(LogicState val) {
	if (_platform->_pins.Drives[_id] == val) return;

	_platform->_pins.Drives[_id] = val;

	const auto netId = _platform->_pins.NetIds[_id];
	_platform->_nets.DirtyImmediate.Mark(netId);

	// Mirrors the write into the flat per-net drive array the solver reads;
	// pre-bake this indexes the dummy slot 0, which is harmless.
	const auto fastIdx = _platform->_pins.FlatDriveIndices[_id];
	_platform->_nets.FlatDrives[fastIdx] = static_cast<uint8_t>(val);
}

inline LogicState Pin::Read() const {
	const auto netId = _platform->_pins.NetIds[_id];
	return _platform->_nets.CurrentValues[netId];
}

} // namespace Sim
