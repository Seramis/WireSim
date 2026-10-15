#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::CommonOps {

// --- Internal Write-Back Step ---

/**
 * The final cycle of any Read-Modify-Write or Store operation.
 * Drives the modified _tVal to the data bus.
 */
inline void FinalWrite(Cpu* c) {
	Addressing::WriteCycle(c, c->_tAddr, c->_tVal);
}

// --- Shared Micro-ops ---

/**
 * Fetches data from the calculated _tAddr into the _tVal scratchpad. This is
 * the first of the two reads a W65C02S RMW instruction performs.
 */
inline void Read_TVal(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		c->_tVal = data;
	});
}

/**
 * A step applied to a Read-Modify-Write instruction's original memory value
 * (as freshly re-read this cycle, mirroring real hardware rather than trusting
 * a value latched two cycles earlier). Must set `_tVal` to the result and
 * update whatever flags it affects — these vary per instruction family
 * (INC/DEC/shifts touch N/Z/Carry; RMB/SMB touch nothing; TSB/TRB touch only
 * Zero, computed from the *original* value before modification).
 */
using RMWStep = void (*)(Cpu*, uint8_t original);

/**
 * The W65C02S's Read-Modify-Write bus pattern at the effective address: two
 * reads followed by one write. (The NMOS 6502 is the opposite shape: one read,
 * two writes — see `docs/w65c02s.pdf` Table 7-1, p.29.) `Read_TVal` already
 * performed the first read; this performs the second ("dummy") read, applies
 * `step`, and enqueues the real write.
 */
inline void RMW(Cpu* c, RMWStep step) {
	Addressing::ReadCycle(c, c->_tAddr, [c, step](uint8_t original) {
		step(c, original);
		c->EnqueueFront(&FinalWrite);
	});
}

} // namespace W65C02Impl::CommonOps
