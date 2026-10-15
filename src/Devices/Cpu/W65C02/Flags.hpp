#pragma once
#include <cstdint>

namespace W65C02Impl {
enum class Flags : uint8_t {
	Carry = 0x01,
	Zero = 0x02,
	InterruptDisable = 0x04,
	Decimal = 0x08,
	Break = 0x10,
	Unused = 0x20,
	Overflow = 0x40,
	Negative = 0x80
};

inline constexpr uint8_t operator|(Flags a, Flags b) {
	return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}
} // namespace W65C02Impl