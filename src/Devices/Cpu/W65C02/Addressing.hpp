#pragma once
#include "W65C02Impl.hpp"

namespace W65C02Impl::Addressing {

// --- Generic Bus Cycle Helpers ---
//
// Every micro-op follows the same two-call protocol: on the first call (address
// phase) drive the address/rw/data-out pins and return; on the second call (latch
// phase) read the captured input and run the completion logic. These wrap that
// boilerplate so call sites only state the address, the value (for writes), and
// what happens once the cycle completes. Template + inline keeps this zero-cost
// after inlining, same as the raw-function-pointer `MicroOp` path.

template <typename OnLatch> inline void ReadCycle(Cpu* c, uint16_t addr, OnLatch onLatch) {
	if (c->AddressPhase(addr)) return;
	uint8_t data;
	if (c->LatchPhase(data)) onLatch(data);
}

inline void ReadCycle(Cpu* c, uint16_t addr) {
	ReadCycle(c, addr, [](uint8_t) {});
}

template <typename OnLatch> inline void WriteCycle(Cpu* c, uint16_t addr, uint8_t value, OnLatch onLatch) {
	if (c->AddressPhase(addr, false)) {
		c->pins.Dout = value;
		return;
	}
	uint8_t dummy;
	if (c->LatchPhase(dummy)) onLatch();
}

inline void WriteCycle(Cpu* c, uint16_t addr, uint8_t value) {
	WriteCycle(c, addr, value, [] {});
}

// --- Shared Utilities ---

inline void DummyReadPC(Cpu* c) {
	ReadCycle(c, c->PC);
}

inline void DummyFixCycle(Cpu* c) {
	// Cycle 5: Fixed address is driven, but result is ignored.
	ReadCycle(c, c->_tAddr);
}

// Real hardware spends a cycle reading from the unindexed base address while the
// index addition happens internally, before the indexed address is used.
inline void ZpXDummy(Cpu* c) {
	ReadCycle(c, c->_tAddr, [c](uint8_t) {
		// Zero Page indexing always wraps within page 0
		c->_tAddr = static_cast<uint8_t>(c->_tAddr + c->X);
	});
}

inline void ZpYDummy(Cpu* c) {
	ReadCycle(c, c->_tAddr, [c](uint8_t) {
		c->_tAddr = static_cast<uint8_t>(c->_tAddr + c->Y);
	});
}

// Helper for internal use to handle ZP indexing (ZpX, ZpY)
inline void ZpIndexed(Cpu* c, Cpu::MicroOp dummy) {
	ReadCycle(c, c->PC, [c, dummy](uint8_t baseAddr) {
		c->_tAddr = baseAddr;
		c->PC++;
		c->EnqueueFront(dummy);
	});
}

// W65C02S: the speculative/dummy cycle during indexed addressing re-reads the
// last instruction byte (PC-1) instead of NMOS's "wrong guess" address, so a
// page crossing never causes a read of an arbitrary, possibly side-effecting
// location (datasheet Table 7-1, p.29: "Indexed addressing across page
// boundary ... W65C02S: Extra read of last instruction byte."). When no
// crossing occurs, this cycle's read IS the genuine data/target address, so
// the normally-computed (and here correct) address is used instead.
inline uint16_t IndexProbeAddr(Cpu* c, uint16_t baseAddr, uint16_t effective, bool pageCrossed) {
	if (pageCrossed) return static_cast<uint16_t>(c->PC - 1);
	return (baseAddr & 0xFF00) | (effective & 0x00FF);
}

// Helper for internal use to handle Absolute indexing fixups (stores, and
// JMP (abs,X)). Every caller pays exactly this one fixup cycle, never a
// second one for crossing: Table 4-1 (p.20) note 1 (the page-boundary note)
// is attached to rows 3/4 (plain indexed reads, which use IndexedRead's own
// carry-dependent optimization below instead) but *not* to row 2 (Absolute
// Indexed Indirect, used only by JMP (abs,X)) — and separately spells out
// "Add 1 cycle for STA abs,X regardless of boundary crossing" for stores.
inline void Abs_Calc(Cpu* c, uint8_t index) {
	uint16_t baseAddr = c->_tAddr;
	uint16_t effective = static_cast<uint16_t>(baseAddr + index);
	bool pageCrossed = (baseAddr & 0xFF00) != (effective & 0xFF00);

	// Cycle 4: Carry out the addition of the low byte.
	ReadCycle(c, IndexProbeAddr(c, baseAddr, effective, pageCrossed), [c, effective](uint8_t) {
		c->_tAddr = effective;
	});
}

// --- Zero Page ---

inline void Zp(Cpu* c) {
	ReadCycle(c, c->PC, [c](uint8_t addr) {
		c->_tAddr = addr;
		c->PC++;
	});
}

inline void ZpX(Cpu* c) {
	ZpIndexed(c, &ZpXDummy);
}
inline void ZpY(Cpu* c) {
	ZpIndexed(c, &ZpYDummy);
}

// --- Zero Page Indirect ---

inline void ZpInd(Cpu* c) {
	ReadCycle(c, c->PC, [c](uint8_t addr) {
		c->_tAddr = addr;
		c->PC++;
	});
}

// --- Absolute ---

inline void AbsLow(Cpu* c) {
	ReadCycle(c, c->PC, [c](uint8_t low) {
		c->_tAddrLow = low;
		c->PC++;
	});
}

inline void AbsHigh(Cpu* c) {
	ReadCycle(c, c->PC, [c](uint8_t high) {
		c->_tAddr = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
		c->PC++;
	});
}

// --- Indirect Helpers ---

inline void Ind_Ptr(Cpu* c) {
	ReadCycle(c, c->PC, [c](uint8_t zpPtr) {
		c->PC++;
		c->_tAddr = zpPtr;
	});
}

inline void Ind_FetchLow(Cpu* c) {
	// Fetch low byte from ZP location
	ReadCycle(c, static_cast<uint16_t>(c->_tAddr & 0x00FF), [c](uint8_t low) {
		c->_tAddrLow = low;
	});
}

inline void Ind_FetchHigh(Cpu* c) {
	// Fetch high byte from (ZP + 1), wrapping at $FF
	uint16_t pointerHighAddr = (c->_tAddr + 1) & 0x00FF;
	ReadCycle(c, pointerHighAddr, [c](uint8_t high) {
		c->_tAddr = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
	});
}

// --- Absolute Indexed Indirect (JMP (addr,X)) ---

inline void AbsX_Ind_FetchLow(Cpu* c) {
	ReadCycle(c, c->_tAddr, [c](uint8_t low) {
		c->_tAddrLow = low;
	});
}

inline void AbsX_Ind_FetchHigh(Cpu* c) {
	ReadCycle(c, static_cast<uint16_t>(c->_tAddr + 1), [c](uint8_t high) {
		c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
	});
}

// --- Register Fixups ---

// Used for address-only consumers (JMP (abs,X)) and store fixups, where the
// dummy cycle's data is never the final result.
inline void FixX(Cpu* c) {
	Abs_Calc(c, c->X);
}
inline void FixY(Cpu* c) {
	Abs_Calc(c, c->Y);
}

// --- Indexed Read (abs,X / abs,Y / (zp),Y data consumers, and RMW abs,X/Y) ---

// On real hardware, cycle 4 (the dummy-looking access at the unindexed-high /
// indexed-low address) IS the data fetch when the index doesn't carry into the
// high byte. Only a page crossing costs a genuine extra cycle. `use` receives
// whichever byte ends up being the correct one.
using ValueUser = void (*)(Cpu*, uint8_t);

inline void FinishIndexedRead(Cpu* c) {
	ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		c->_tContinuation(c, data);
	});
}

inline void IndexedRead(Cpu* c, uint8_t index, ValueUser use) {
	uint16_t baseAddr = c->_tAddr;
	uint16_t effective = static_cast<uint16_t>(baseAddr + index);
	bool pageCrossed = (baseAddr & 0xFF00) != (effective & 0xFF00);

	ReadCycle(c, IndexProbeAddr(c, baseAddr, effective, pageCrossed), [c, effective, use, pageCrossed](uint8_t data) {
		c->_tAddr = effective;

		if (pageCrossed) {
			c->_tContinuation = use;
			c->EnqueueFront(&FinishIndexedRead);
		} else {
			use(c, data);
		}
	});
}

template <ValueUser Use> inline void IndexedReadX(Cpu* c) {
	IndexedRead(c, c->X, Use);
}

template <ValueUser Use> inline void IndexedReadY(Cpu* c) {
	IndexedRead(c, c->Y, Use);
}
} // namespace W65C02Impl::Addressing
