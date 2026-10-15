#pragma once
#include <cstdint>
#include <string>

namespace Sim {

struct LogicState {
	enum Level : uint8_t {
		F = 0,
		Lw = 1 << 0, // 1
		Hw = 1 << 1, // 2
		L = 1 << 2,	 // 4
		H = 1 << 3,	 // 8
		E = 1 << 7	 // 128
	};

	uint8_t Value;

	// Constructors
	constexpr LogicState() noexcept : Value(F) {}
	constexpr LogicState(Level v) noexcept : Value(v) {}
	constexpr LogicState(uint8_t v) noexcept : Value(v) {}
	constexpr LogicState(bool v) noexcept : Value(v ? H : L) {}

	constexpr operator bool() const noexcept {
		return IsHigh();
	}
	constexpr explicit operator uint8_t() const noexcept {
		return Value;
	}

	constexpr bool IsHigh() const noexcept {
		return (Value & (H | Hw)) != 0;
	}
	constexpr bool IsLow() const noexcept {
		return (Value & (L | Lw)) != 0;
	}
	constexpr bool IsFloating() const noexcept {
		return Value == F;
	}
	constexpr bool IsError() const noexcept {
		return Value == E;
	}

	constexpr LogicState ActiveHigh() const noexcept {
		return IsHigh() ? H : L;
	}
	constexpr LogicState ActiveLow() const noexcept {
		return IsLow() ? H : L;
	}
	constexpr LogicState TriHigh() const noexcept {
		return IsHigh() ? H : F;
	}
	constexpr LogicState TriLow() const noexcept {
		return IsLow() ? H : F;
	}

	// Operator Overloads
	constexpr bool operator==(const LogicState other) const noexcept {
		return Value == other.Value;
	}
	constexpr bool operator!=(const LogicState other) const noexcept {
		return Value != other.Value;
	}

	// Logic OR (Algebraic)
	constexpr LogicState operator|(const LogicState b) const noexcept {
		return LogicState(static_cast<uint8_t>(Value | b.Value));
	}

	// Declarations for LUT-driven operators (defined below)
	constexpr LogicState operator!() const noexcept;
	constexpr LogicState operator&(const LogicState b) const noexcept;

	std::string ToString() const noexcept {
		switch (Value) {
			case H: return "H";
			case Hw: return "Hw";
			case L: return "L";
			case Lw: return "Lw";
			case F: return "?";
			default: return "E";
		}
	}

	static constexpr LogicState ActiveLow(const bool condition) noexcept {
		return condition ? L : H;
	}

	static constexpr LogicState ActiveHigh(const bool condition) noexcept {
		return condition ? H : L;
	}

	static constexpr LogicState TriLow(const bool condition) noexcept {
		return condition ? L : F;
	}

	static constexpr LogicState TriHigh(const bool condition) noexcept {
		return condition ? H : F;
	}
};

// -----------------------------------------------------------------------------
// Implementation detail: the drive-resolution lookup tables backing
// LogicState's operators. Not part of the device/machine-author API, but
// Platform's hot path (ProcessNetsAndImmediates) reads LogicLUT directly.
// -----------------------------------------------------------------------------
namespace detail {

// Resolves a net's combined drive-bit flags to its logic level.
struct LogicTable {
	LogicState::Level data[256]{};

	constexpr LogicTable() {
		for (int i = 0; i < 256; i++) {
			data[i] = Compute(static_cast<uint8_t>(i));
		}
	}

	static constexpr LogicState::Level Compute(uint8_t i) {
		uint8_t lv = i;
		uint8_t strongs = lv & 0b1100; // 8|4
		uint8_t weaks = lv & 0b0011;   // 2|1
		bool strongConflict = (strongs == 0b1100);
		bool weakConflict = (weaks == 0b0011);

		if ((lv & 0x80) || strongConflict || (weakConflict && strongs == 0)) return LogicState::Level::E;
		if (strongs != 0) return (strongs & 8) ? LogicState::Level::H : LogicState::Level::L;
		if (weaks != 0) return (weaks & 2) ? LogicState::Level::Hw : LogicState::Level::Lw;

		return LogicState::Level::F;
	}
};

// AND LUT
struct ANDTable {
	LogicState::Level data[256][256]{};

	constexpr ANDTable() {
		for (int a = 0; a < 256; ++a) {
			for (int b = 0; b < 256; ++b) {
				data[a][b] = ComputeANDEntry(static_cast<uint8_t>(a), static_cast<uint8_t>(b));
			}
		}
	}

	constexpr LogicState::Level ComputeANDEntry(uint8_t a_raw, uint8_t b_raw) {
		LogicState a(a_raw);
		LogicState b(b_raw);

		if (a.IsError() || b.IsError()) return LogicState::E;

		constexpr uint8_t valid_mask =
			LogicState::F | LogicState::Lw | LogicState::Hw | LogicState::L | LogicState::H | LogicState::E;
		if ((a_raw & ~valid_mask) != 0 || (b_raw & ~valid_mask) != 0) return LogicState::E;

		bool a_high = a.IsHigh();
		bool b_high = b.IsHigh();
		bool a_low = a.IsLow();
		bool b_low = b.IsLow();

		if (a_high && b_high) {
			bool is_strong = (a_raw & LogicState::H) && (b_raw & LogicState::H);
			return is_strong ? LogicState::H : LogicState::Hw;
		}

		if (a_low || b_low) {
			bool is_strong = (a_raw & LogicState::L) || (b_raw & LogicState::L);
			return is_strong ? LogicState::L : LogicState::Lw;
		}

		return LogicState::F;
	}
};

// The static constexpr tables
inline constexpr LogicTable LogicLUT{};
inline constexpr ANDTable AND_LUT{};

} // namespace detail

// ============================================================================
// Zero-Branch Member Operator Definitions
// ============================================================================

constexpr LogicState LogicState::operator!() const noexcept {
	// Swaps bit 0 <-> bit 1 (Lw <-> Hw) and bit 2 <-> bit 3 (L <-> H)
	uint8_t swapped = ((Value & 0x05) << 1) | ((Value & 0x0A) >> 1);
	// Preserves Floating (0) and Error (128)
	return LogicState(static_cast<uint8_t>((Value & 0x80) | swapped));
}

constexpr LogicState LogicState::operator&(LogicState b) const noexcept {
	return detail::AND_LUT.data[Value][b.Value];
}

} // namespace Sim
