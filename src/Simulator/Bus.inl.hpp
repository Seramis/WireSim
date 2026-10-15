#pragma once
#include "Bus.hpp"
#include "src/Simulator/Platform.hpp"

// -----------------------------------------------------------------------------
// Bus::Read needs Platform's complete type (it reaches into its private
// storage), which isn't available in Bus.hpp -- Platform is only
// forward-declared there. This file holds that definition so it stays next
// to Bus's declaration instead of buried in Platform.hpp; it relies on
// Platform already being fully defined at the point it's included, so only
// Platform.hpp includes it, after its own class body.
// -----------------------------------------------------------------------------
namespace Sim {

template <typename T> inline T Bus::Read() const {
	EnsureCache();

	T val = 0;
	const int count = static_cast<int>(_pinIds.size());
	const auto* netValues = _platform->_nets.CurrentValues.data();

	if (_cacheReady) {
		const int* cachedNets = _cachedNetIds.data();
		for (int i = 0; i < count; ++i) {
			val |= (static_cast<T>(netValues[cachedNets[i]].IsHigh()) << i);
		}
		return val;
	}

	// Pre-bake fallback: the net cache above isn't valid yet.
	const int* pinIds = _pinIds.data();
	const int* pinNets = _platform->_pins.NetIds.data();

	for (int i = 0; i < count; ++i) {
		const int netId = pinNets[pinIds[i]];
		const bool isHigh = netValues[netId].IsHigh();
		val |= (static_cast<T>(isHigh) << i);
	}
	return val;
}

} // namespace Sim
