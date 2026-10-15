#include "Platform.hpp"

namespace Sim {

Platform::Platform() {}

// -----------------------------------------------------------------------------
// Link Logic
// -----------------------------------------------------------------------------
void Platform::Link(Pin p1, Pin p2) {
	int net1 = _pins.NetIds[p1._id];
	int net2 = _pins.NetIds[p2._id];
	if (net1 == net2) return;

	// Merge Net2 into Net1
	auto& pinsToMove = _nets.PinIds[net2];
	for (int pinId : pinsToMove) {
		_pins.NetIds[pinId] = net1;
		_nets.PinIds[net1].push_back(pinId);
	}
	pinsToMove.clear();

	auto& tImm = _nets.ImmediateTriggerIds[net2];
	_nets.ImmediateTriggerIds[net1].insert(_nets.ImmediateTriggerIds[net1].end(), tImm.begin(), tImm.end());
	tImm.clear();

	auto& tStable = _nets.StableTriggerIds[net2];
	_nets.StableTriggerIds[net1].insert(_nets.StableTriggerIds[net1].end(), tStable.begin(), tStable.end());
	tStable.clear();

	_nets.DirtyImmediate.Mark(net1);
}

void Platform::Link(Bus b1, Bus b2) {
	if (b1.Count() != b2.Count()) throw std::runtime_error("Bus width mismatch");
	for (int i = 0; i < b1.Count(); i++)
		Link(b1[i], b2[i]);
}

void Platform::RegisterDevice(Device* device, Device::LogicFn fn) {
	int newId = (int)_devices.Instances.size();
	device->_deviceId = newId;
	_devices.EnsureCapacity(newId);
	_devices.Instances[newId] = device;
	_devices.LogicFns[newId] = fn;
}

// -----------------------------------------------------------------------------
// Simulation Core
// -----------------------------------------------------------------------------

void Platform::Bake() {
	int netCount = (int)_nets.CurrentValues.size();

	// 1. Calculate total pins to size the FlatDrives array
	int totalPins = 0;
	for (const auto& pList : _nets.PinIds) {
		totalPins += (int)pList.size();
	}

	_nets.DriveRange.resize(netCount);
	_nets.FlatDrives.assign(totalPins, (uint8_t)LogicState::F);

	if (_pins.FlatDriveIndices.size() < _pins.Names.size()) {
		_pins.FlatDriveIndices.resize(_pins.Names.size(), 0);
	}

	// 2. Map Pins to FlatDrives and set Ranges
	int currentOffset = 0;
	for (int i = 0; i < netCount; i++) {
		int count = (int)_nets.PinIds[i].size();
		_nets.DriveRange[i] = {currentOffset, count};

		for (int k = 0; k < count; k++) {
			int pinId = _nets.PinIds[i][k];
			int fastIdx = currentOffset + k;
			_pins.FlatDriveIndices[pinId] = fastIdx;
			_nets.FlatDrives[fastIdx] = (uint8_t)_pins.Drives[pinId];
		}
		currentOffset += count;
	}

	// 3. Flatten Triggers (Immediate)
	_nets.ImmediateTriggerRange.resize(netCount);
	_nets.FlatImmediateTriggers.clear();
	for (int i = 0; i < netCount; i++) {
		_nets.ImmediateTriggerRange[i] = {
			(int)_nets.FlatImmediateTriggers.size(), (int)_nets.ImmediateTriggerIds[i].size()
		};
		for (int tId : _nets.ImmediateTriggerIds[i]) {
			auto& t = _triggers.Entries[tId];
			_nets.FlatImmediateTriggers.push_back({t.DeviceId, 1ULL << t.BitIndex});
		}
	}

	// 4. Flatten Triggers (Stable)
	_nets.StableTriggerRange.resize(netCount);
	_nets.FlatStableTriggers.clear();
	for (int i = 0; i < netCount; i++) {
		_nets.StableTriggerRange[i] = {(int)_nets.FlatStableTriggers.size(), (int)_nets.StableTriggerIds[i].size()};
		for (int tId : _nets.StableTriggerIds[i]) {
			auto& t = _triggers.Entries[tId];
			_nets.FlatStableTriggers.push_back({t.DeviceId, 1ULL << t.BitIndex});
		}
	}

	// 5. Optimization Flags
	_nets.IsDeadEnd.assign(netCount, false);
	_nets.HasStableTriggers.assign(netCount, false);
	for (int i = 0; i < netCount; i++) {
		_nets.IsDeadEnd[i] = _nets.ImmediateTriggerIds[i].empty() && _nets.StableTriggerIds[i].empty();
		_nets.HasStableTriggers[i] = !_nets.StableTriggerIds[i].empty();
		_nets.LastStableValues[i] = _nets.CurrentValues[i]; // Initialize stable tracking
	}

	_isBaked = true;
}

inline void Platform::ProcessNetsAndImmediates() {
	auto* flatDrives = _nets.FlatDrives.data();
	auto* driveRanges = _nets.DriveRange.data();
	auto* values = _nets.CurrentValues.data();
	auto* immRanges = _nets.ImmediateTriggerRange.data();
	auto* flatTriggers = _nets.FlatImmediateTriggers.data();
	auto* pendingImmediate = _devices.PendingImmediate.data();
	auto* instances = _devices.Instances.data();
	auto* logicFns = _devices.LogicFns.data();
	auto* isDeadEnd = _nets.IsDeadEnd.data();
	auto* hasStableTriggers = _nets.HasStableTriggers.data();

	while (!_nets.DirtyImmediate.IsEmpty()) {

		// Use Visit lambda to avoid vector allocs
		_nets.DirtyImmediate.Visit([&](const int netId) {
			const auto& range = driveRanges[netId];

			// Fast path: most nets have exactly one driver, so skip the
			// aggregation loop entirely for them (only genuinely shared
			// nets -- bus contention, wired-OR -- need the scan).
			uint8_t aggregate;
			if (range.Count == 1) [[likely]] {
				aggregate = flatDrives[range.Offset];
			} else {
				aggregate = 0;
				const auto end = range.Offset + range.Count;
				for (int k = range.Offset; k < end; k++) {
					aggregate |= flatDrives[k];
				}
			}

			const LogicState newValue = detail::LogicLUT.data[aggregate];
			const LogicState oldValue = values[netId];
			values[netId] = newValue;

			if (isDeadEnd[netId]) return;
			// Only queue a stable-side re-check when the resolved value
			// actually moved this round. "Dirty" means a driver touched
			// the net, not that its aggregate necessarily changed (a
			// multi-driver net can settle back to the same value it had);
			// ProcessStableDevices still re-checks against LastStableValues
			// before dispatching, so this only trims redundant marks, it
			// never changes what fires.
			if (hasStableTriggers[netId] && newValue != oldValue) _nets.DirtyStable.Mark(netId);

			// Queue Triggers
			const auto& tRange = immRanges[netId];
			const int tEnd = tRange.Offset + tRange.Count;
			for (int k = tRange.Offset; k < tEnd; k++) {
				auto& trig = flatTriggers[k];
				pendingImmediate[trig.DeviceId] |= trig.Mask;
				_devices.DirtyImmediate.Mark(trig.DeviceId);
			}
		});

		// Process Devices
		_devices.DirtyImmediate.Visit([&](const int devId) {
			const auto mask = pendingImmediate[devId];
			pendingImmediate[devId] = 0;
			logicFns[devId](instances[devId], mask);
		});
	}
}

inline void Platform::ProcessStableDevices() {
	const auto* values = _nets.CurrentValues.data();
	auto* instances = _devices.Instances.data();
	auto* logicFns = _devices.LogicFns.data();

	_nets.DirtyStable.Visit([&](const int netId) {
		if (_nets.LastStableValues[netId] == values[netId]) return;

		const auto& tRange = _nets.StableTriggerRange[netId];
		const auto tEnd = tRange.Offset + tRange.Count;
		for (int k = tRange.Offset; k < tEnd; k++) {
			auto& trig = _nets.FlatStableTriggers[k];
			_devices.PendingStable[trig.DeviceId] |= trig.Mask;
			_devices.DirtyStable.Mark(trig.DeviceId);
		}
		_nets.LastStableValues[netId] = values[netId];
	});

	_devices.DirtyStable.Visit([&](const int devId) {
		const auto mask = _devices.PendingStable[devId];
		_devices.PendingStable[devId] = 0;
		logicFns[devId](instances[devId], mask);
	});
}

void Platform::Cycle() {

	ProcessNetsAndImmediates();

	if (ProcessExternalCommands()) [[unlikely]] {
		ProcessNetsAndImmediates();
	}

	ProcessStableDevices();
}

inline bool Platform::ProcessExternalCommands() {
	if (!_hasCommands.load(std::memory_order_acquire)) [[likely]] {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(_commandMutex);
		if (_externalCommands.empty()) {
			_hasCommands.store(false, std::memory_order_relaxed);
			return false;
		}
		// Swap rather than copy, and run the commands outside the lock so
		// Enqueue() stays available to other threads while they execute.
		_commandBuffer.swap(_externalCommands);
		_hasCommands.store(false, std::memory_order_relaxed);
	}

	for (auto& cmd : _commandBuffer) {
		cmd();
	}
	_commandBuffer.clear(); // Keeps capacity allocated for next cycle.

	return true;
}

} // namespace Sim
