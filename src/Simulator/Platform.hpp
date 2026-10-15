#pragma once
#include "Bus.hpp"
#include "Device.hpp"
#include "Diagnostics.hpp"
#include "LogicState.hpp"
#include "Pin.hpp"
#include "detail/DirtyTagSet.hpp"
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace Sim {

// -----------------------------------------------------------------------------
// Platform: owns the netlist (nets, pins, devices, triggers), bakes it into
// a flat run-time layout once (Bake), and drives the two-phase simulation
// cycle (Cycle). See overview.md for the bake-then-run and trigger model.
// -----------------------------------------------------------------------------
class Platform {
	friend struct Pin;
	friend struct Bus;

  public:
	Platform();
	void Link(Pin p1, Pin p2);
	void Link(Bus b1, Bus b2);

	// Start() is a template so the per-cycle callback (almost always a
	// small lambda) can be inlined into the run loop instead of being
	// called through a std::function's type-erased indirection -- this
	// runs once per half clock cycle for the simulation's entire lifetime.
	void Start() {
		Start([] {});
	}

	template <typename F> void Start(F&& onBeforeCycle) {
		_running = true;
		Bake();

		while (_running.load(std::memory_order_relaxed)) {
			onBeforeCycle();
			if (!_running.load(std::memory_order_relaxed)) [[unlikely]]
				break;
			Cycle();
		}
	}

	void Stop() {
		_running = false;
	}
	bool IsRunning() const {
		return _running;
	}
	bool IsBaked() const {
		return _isBaked;
	}

	Diagnostics& GetDiagnostics() {
		return _diagnostics;
	}

	void Enqueue(std::function<void()> cmd) {
		{
			std::lock_guard<std::mutex> lock(_commandMutex);
			_externalCommands.push_back(std::move(cmd));
		}
		_hasCommands.store(true, std::memory_order_release);
	}

	template <typename F> auto EnqueueWithFuture(F&& func) -> std::future<typename std::invoke_result<F>::type> {
		using ReturnType = typename std::invoke_result<F>::type;

		// Use a packaged_task for cleaner void/non-void handling
		auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(func));
		std::future<ReturnType> fut = task->get_future();

		// Call your existing void Enqueue
		this->Enqueue([task]() {
			(*task)();
		});

		return fut;
	}

  private:
	// Per-device SoA table, indexed by DeviceId.
	struct DeviceTable {
		std::vector<Device*> Instances;
		// Parallel to Instances: a type-erased thunk captured once per device
		// (see Pin::AddTrigger below) that calls that device's concrete
		// Logic() directly. Dispatch reads this flat array instead of
		// chasing a vtable pointer through the (possibly cold) device object.
		std::vector<Device::LogicFn> LogicFns;
		std::vector<uint64_t> PendingImmediate; // Trigger bits awaiting dispatch this immediate pass
		std::vector<uint64_t> PendingStable;	// Trigger bits awaiting dispatch this stable pass
		detail::DirtyTagSet DirtyImmediate;		// DeviceIds with a nonzero PendingImmediate mask
		detail::DirtyTagSet DirtyStable;		// DeviceIds with a nonzero PendingStable mask

		void EnsureCapacity(int devId) {
			if ((size_t)devId >= Instances.size()) {
				size_t newSize = std::max((size_t)devId + 1, Instances.size() * 2);
				Instances.resize(newSize, nullptr);
				LogicFns.resize(newSize, nullptr);
				PendingImmediate.resize(newSize, 0);
				PendingStable.resize(newSize, 0);
			}
		}
	};

	// Per-net SoA table, indexed by NetId.
	struct NetTable {
		std::vector<LogicState> CurrentValues;
		std::vector<LogicState> LastStableValues;

		// Connectivity Graph (pre-bake; PinIds/ImmediateTriggerIds/StableTriggerIds
		// mirror _pins.NetIds, just read the other direction)
		std::vector<std::vector<int>> PinIds;
		std::vector<std::vector<int>> ImmediateTriggerIds;
		std::vector<std::vector<int>> StableTriggerIds;

		detail::DirtyTagSet DirtyImmediate; // NetIds whose drive changed, pending aggregate resolve
		detail::DirtyTagSet DirtyStable;	// NetIds whose resolved value may need a stable re-check

		// Baked Data
		struct FlatTrigger {
			int DeviceId;
			uint64_t Mask;
		};
		struct Range {
			int Offset;
			int Count;
		};

		// Optimization: Contiguous memory for drive values sorted by Net.
		// Instead of chasing pointers, we scan this array linearly.
		std::vector<uint8_t> FlatDrives;

		std::vector<Range> DriveRange;			  // Map NetId -> Range in FlatDrives
		std::vector<Range> ImmediateTriggerRange; // Map NetId -> Range in FlatImmediateTriggers
		std::vector<Range> StableTriggerRange;	  // Map NetId -> Range in FlatStableTriggers

		std::vector<FlatTrigger> FlatImmediateTriggers;
		std::vector<FlatTrigger> FlatStableTriggers;

		std::vector<uint8_t> IsDeadEnd;
		std::vector<uint8_t> HasStableTriggers;

		int NewId() {
			int id = (int)CurrentValues.size();
			CurrentValues.push_back(LogicState::F);
			LastStableValues.push_back(LogicState::F);
			PinIds.emplace_back();
			ImmediateTriggerIds.emplace_back();
			StableTriggerIds.emplace_back();
			return id;
		}
	};

	// Per-pin SoA table, indexed by PinId.
	struct PinTable {
		std::vector<std::string> Names;
		std::vector<LogicState> Drives;
		std::vector<int> NetIds;

		// Optimization: Map PinId -> Index in _nets.FlatDrives
		std::vector<int> FlatDriveIndices;

		int NewId() {
			int id = (int)Names.size();
			Names.push_back("");
			Drives.push_back(LogicState::F);
			NetIds.push_back(-1);
			FlatDriveIndices.push_back(0); // Default to safe 0
			return id;
		}
	};

	// Flat table of registered (device, bit) trigger registrations, indexed by TriggerId.
	struct TriggerTable {
		struct Entry {
			int DeviceId;
			uint8_t BitIndex;
		};
		std::vector<Entry> Entries;

		int NewId(int deviceId, uint8_t bit) {
			int id = (int)Entries.size();
			Entries.push_back({deviceId, bit});
			return id;
		}
	};

	DeviceTable _devices;
	NetTable _nets;
	PinTable _pins;
	TriggerTable _triggers;
	Diagnostics _diagnostics;

	std::atomic<bool> _running;
	bool _isBaked = false;

	std::mutex _commandMutex;
	std::vector<std::function<void()>> _externalCommands; // The "Inbox"
	std::vector<std::function<void()>> _commandBuffer;	  // The simulation's local work area
	std::atomic<bool> _hasCommands{false};				  // The "Gatekeeper"

	void Bake();
	void Cycle();

	// Registers `device` under a fresh device id and records its type-erased
	// Logic thunk. Called from Pin::AddTrigger<T> (below) the first time a
	// given device instance registers a trigger.
	void RegisterDevice(Device* device, Device::LogicFn fn);

	bool ProcessExternalCommands();
	void ProcessNetsAndImmediates();
	void ProcessStableDevices();
};

} // namespace Sim

// Pin::AddTrigger/Write/Read and Bus::Read reach into Platform's private
// storage, so they need Platform's complete type at the point they're
// defined -- unavailable in Pin.hpp/Bus.hpp, where Platform is only
// forward-declared. Rather than define them inline here, pull in the .inl
// files colocated with Pin.hpp/Bus.hpp, now that Platform is fully defined;
// each reopens `namespace Sim` itself.
#include "Bus.inl.hpp" // IWYU pragma: keep
#include "Pin.inl.hpp" // IWYU pragma: keep
