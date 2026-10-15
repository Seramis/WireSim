#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <vector>

namespace Sim::detail {

// -----------------------------------------------------------------------------
// DirtyTagSet: a sparse, visit-and-clear set of small integer ids.
// Internal to Platform's dirty-net/dirty-device tracking; not part of the
// device/machine-author API.
// -----------------------------------------------------------------------------
class DirtyTagSet {
  private:
	std::vector<uint64_t> _bits;
	bool _hasData = false;

  public:
	DirtyTagSet() {
		_bits.resize(1, 0);
	}

	inline bool IsEmpty() const {
		return !_hasData;
	}

	void Mark(int index) {
		const size_t wordIndex = index >> 6;
		if (wordIndex >= _bits.size()) [[unlikely]] {
			size_t newSize = std::max(_bits.size() * 2, wordIndex + 1);
			_bits.resize(newSize, 0);
		}
		_bits[wordIndex] |= (1ULL << (index & 63));
		_hasData = true;
	}

	void Clear() {
		std::fill(_bits.begin(), _bits.end(), 0);
		_hasData = false;
	}

	template <typename Func> inline void Visit(const Func func) {
		if (!_hasData) return;

		uint64_t* __restrict ptr = _bits.data();
		const size_t size = _bits.size();

		for (size_t i = 0; i < size; ++i) {
			uint64_t val = ptr[i];
			if (val == 0) continue;

			// Clear the dirty word in memory
			ptr[i] = 0;

			const int baseIdx = static_cast<int>(i) << 6;

			// Iterate over all set bits in the 64-bit word
			do {
				const auto bitIdx = std::countr_zero(val);

				func(baseIdx + static_cast<int>(bitIdx));
				val &= (val - 1); // Clear lowest set bit
			} while (val != 0);
		}

		_hasData = false;
	}
};

} // namespace Sim::detail
