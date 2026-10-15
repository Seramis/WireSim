#pragma once
#include "Pin.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Sim {

class Platform;

// -----------------------------------------------------------------------------
// Bus: a fixed-width group of Pins (e.g. an address or data bus), indexable
// bit by bit and writable/readable as an integer.
// -----------------------------------------------------------------------------
struct Bus {
  public:
	static Bus New(Platform* p, int width, std::string name = "");
	static Bus From(Platform* p, int width, std::function<Pin(int index)> factory);

	int Count() const {
		return (int)_pinIds.size();
	}
	Pin operator[](const int index) const {
		return {_platform, _pinIds[index]};
	}

	Bus Slice(int start, int end) const;

	void Write(const uint64_t val);
	// Defined in Bus.inl.hpp (included from Platform.hpp): needs Platform's
	// complete type, not just the forward declaration above.
	template <typename T> T Read() const;
	void Clear();

	template <typename T> void AddTrigger(T* device, uint8_t idBit = 0, bool isImmediate = true) {
		for (int id : _pinIds)
			Pin{_platform, id}.AddTrigger(device, idBit, isImmediate);
	}
	template <typename T, typename TEnum> void AddTrigger(T* device, TEnum idBit, bool isImmediate = true) {
		AddTrigger(device, static_cast<uint8_t>(idBit), isImmediate);
	}

  private:
	Platform* _platform;
	std::vector<int> _pinIds;

	// Per-pin net id / fast-drive-slot index, built lazily once Platform has
	// baked (that mapping is frozen from then on) so Read/Write/Clear can
	// skip re-deriving it from Platform's Pins table on every bit. Mutable
	// because Read() is const -- building the cache doesn't change what it
	// returns, only how fast it gets there. Defined in Bus.cpp, where
	// Platform's full type is visible.
	mutable bool _cacheReady = false;
	mutable std::vector<int> _cachedNetIds;
	mutable std::vector<int> _cachedFastIdx;
	void EnsureCache() const;
};

} // namespace Sim
