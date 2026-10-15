#pragma once
#include <inttypes.h>

namespace W65C02Impl {
struct Pins {
	uint16_t Aout;
	uint8_t Din;
	uint8_t Dout;
	bool RWBout;
	bool RDYin;
	bool RESBin;
	bool IRQBin;
	bool NMIBin;
	bool PHI1out;
	bool PHI2out;
	bool PHI2in;
	// No BE pin: this model assumes Bus Enable is permanently tied high (as on
	// Ben Eater's breadboard build, the only machine using this CPU today).
};
} // namespace W65C02Impl