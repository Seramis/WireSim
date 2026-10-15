#include "Bus.hpp"
#include "Platform.hpp"

namespace Sim {

Bus Bus::New(Platform* p, int width, std::string name) {
	Bus b;
	b._platform = p;
	for (int i = 0; i < width; i++) {
		b._pinIds.push_back(Pin::New(p, name + "[" + std::to_string(i) + "]")._id);
	}
	return b;
}

Bus Bus::From(Platform* p, int width, std::function<Pin(int index)> factory) {
	Bus b;
	b._platform = p;
	b._pinIds.reserve(width);
	for (int i = 0; i < width; i++) {
		// Generate the pin using the provided factory function
		Pin pin = factory(i);
		b._pinIds.push_back(pin._id);
	}
	return b;
}

Bus Bus::Slice(int start, int end) const {
	int size = Count();

	// Convert negative indices to positive offsets from the end
	auto normalize = [size](int i) {
		return (i < 0) ? (size + i) : i;
	};

	int actualStart = normalize(start);
	int actualEnd = normalize(end);

	// Bounds clamping
	if (actualStart < 0) actualStart = 0;
	if (actualEnd > size) actualEnd = size;

	Bus sliced;
	sliced._platform = this->_platform;

	// Only copy if the range is valid
	if (actualStart < actualEnd && actualStart < size) {
		auto itStart = _pinIds.begin() + actualStart;
		auto itEnd = _pinIds.begin() + std::min(actualEnd, size);
		sliced._pinIds.assign(itStart, itEnd);
	}

	return sliced;
}

void Bus::EnsureCache() const {
	// Before bake, Pins/FlatDriveIndices can still change (Link merges
	// nets), so callers fall back to re-deriving the mapping on every call.
	if (_cacheReady || !_platform->IsBaked()) return;

	const int count = Count();
	_cachedNetIds.resize(count);
	_cachedFastIdx.resize(count);

	const auto& pins = _platform->_pins;
	for (int i = 0; i < count; ++i) {
		const int pinId = _pinIds[i];
		_cachedNetIds[i] = pins.NetIds[pinId];
		_cachedFastIdx[i] = pins.FlatDriveIndices[pinId];
	}

	_cacheReady = true;
}

void Bus::Write(const uint64_t val) {
	EnsureCache();

	const int count = Count();

	// Hoist platform state pointers
	auto& pins = _platform->_pins;
	auto& nets = _platform->_nets;

	auto* drives = pins.Drives.data();
	auto* flatDrives = nets.FlatDrives.data();
	const int* pinIds = _pinIds.data();

	if (_cacheReady) {
		const int* cachedNets = _cachedNetIds.data();
		const int* cachedFastIdx = _cachedFastIdx.data();

		for (int i = 0; i < count; ++i) {
			const int pinId = pinIds[i];
			const LogicState bitVal = ((val >> i) & 1ULL) ? LogicState::H : LogicState::L;

			if (drives[pinId] == bitVal) continue;

			drives[pinId] = bitVal;
			nets.DirtyImmediate.Mark(cachedNets[i]);
			flatDrives[cachedFastIdx[i]] = static_cast<uint8_t>(bitVal);
		}
		return;
	}

	// Pre-bake fallback: the fast indices above aren't valid yet.
	const auto* netIds = pins.NetIds.data();
	const auto* flatDriveIndices = pins.FlatDriveIndices.data();

	for (int i = 0; i < count; ++i) {
		const int pinId = pinIds[i];
		const LogicState bitVal = ((val >> i) & 1ULL) ? LogicState::H : LogicState::L;

		if (drives[pinId] == bitVal) continue;

		// 1. Update Persistent State
		drives[pinId] = bitVal;

		// 2. Mark Net Dirty
		const int netId = netIds[pinId];
		nets.DirtyImmediate.Mark(netId);

		// 3. Hot Path Update
		const int fastIdx = flatDriveIndices[pinId];
		flatDrives[fastIdx] = static_cast<uint8_t>(bitVal);
	}
}

void Bus::Clear() {
	EnsureCache();

	const int count = Count();

	auto& pins = _platform->_pins;
	auto& nets = _platform->_nets;

	auto* drives = pins.Drives.data();
	auto* flatDrives = nets.FlatDrives.data();
	const int* pinIds = _pinIds.data();

	const auto targetVal = LogicState::F;
	const auto targetValUint8 = static_cast<uint8_t>(targetVal);

	if (_cacheReady) {
		const int* cachedNets = _cachedNetIds.data();
		const int* cachedFastIdx = _cachedFastIdx.data();

		for (int i = 0; i < count; ++i) {
			const int pinId = pinIds[i];
			if (drives[pinId].Value == targetVal) continue;

			drives[pinId] = targetVal;
			nets.DirtyImmediate.Mark(cachedNets[i]);
			flatDrives[cachedFastIdx[i]] = targetValUint8;
		}
		return;
	}

	// Pre-bake fallback: the fast indices above aren't valid yet.
	const auto* netIds = pins.NetIds.data();
	const auto* flatDriveIndices = pins.FlatDriveIndices.data();

	for (int i = 0; i < count; ++i) {
		const int pinId = pinIds[i];

		if (drives[pinId].Value == targetVal) continue;

		drives[pinId] = targetVal;
		const int netId = netIds[pinId];
		nets.DirtyImmediate.Mark(netId);

		const int fastIdx = flatDriveIndices[pinId];
		flatDrives[fastIdx] = targetValUint8;
	}
}

} // namespace Sim
