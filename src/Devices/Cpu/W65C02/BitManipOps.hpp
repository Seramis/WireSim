#pragma once
#include "Addressing.hpp"
#include "CommonOps.hpp"
#include "JumpOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::BitManipOps {

// --- Internal Helpers ---

/**
 * Shared logic for BBR/BBS.
 * Fetches the relative offset and prepares the branch decision.
 */
inline void DoBitBranch(Cpu* c, bool condition) {
	Addressing::ReadCycle(c, c->PC, [c, condition](uint8_t) {
		c->PC++;
		c->_tVal = condition ? 2 : 0;				// 2 = Branch Always, 0 = No Branch
		c->Enqueue(&JumpOps::Step_Branch_Decision); // Uses JumpOps logic
	});
}

/**
 * Shared "zp addressing, read into _tVal, then run `finalStep`" sequencing,
 * used both by BBR/BBS (finalStep = the branch test) and by RMB/SMB
 * (finalStep = the CommonOps::RMW-based bit-set/reset step).
 */
inline void EnqueueZpThenStep(Cpu* c, void (*finalStep)(Cpu*)) {
	c->Enqueue(&Addressing::Zp);
	c->Enqueue(&CommonOps::Read_TVal);
	c->Enqueue(finalStep);
}

// --- SMBx: Set Memory Bit (0-7) ---
// Routed through CommonOps::RMW, the same W65C02S two-read/one-write helper
// used by INC/DEC/shifts. RMB/SMB affect no CPU flags, so the step callback
// only ever touches `_tVal`. The bit mask must be a compile-time literal (the
// micro-op queue stores raw function pointers, not closures), hence one
// Step_* per bit instead of a single parameterized helper.

inline void Step_SMB0(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x01);
	});
}
inline void Step_SMB1(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x02);
	});
}
inline void Step_SMB2(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x04);
	});
}
inline void Step_SMB3(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x08);
	});
}
inline void Step_SMB4(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x10);
	});
}
inline void Step_SMB5(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x20);
	});
}
inline void Step_SMB6(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x40);
	});
}
inline void Step_SMB7(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original | 0x80);
	});
}

inline void SMB0(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB0); // reuses the Zp + Read_TVal + step sequencing
}
inline void SMB1(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB1);
}
inline void SMB2(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB2);
}
inline void SMB3(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB3);
}
inline void SMB4(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB4);
}
inline void SMB5(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB5);
}
inline void SMB6(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB6);
}
inline void SMB7(Cpu* c) {
	EnqueueZpThenStep(c, &Step_SMB7);
}

// --- RMBx: Reset Memory Bit (0-7) ---

inline void Step_RMB0(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x01);
	});
}
inline void Step_RMB1(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x02);
	});
}
inline void Step_RMB2(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x04);
	});
}
inline void Step_RMB3(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x08);
	});
}
inline void Step_RMB4(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x10);
	});
}
inline void Step_RMB5(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x20);
	});
}
inline void Step_RMB6(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x40);
	});
}
inline void Step_RMB7(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original & ~0x80);
	});
}

inline void RMB0(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB0);
}
inline void RMB1(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB1);
}
inline void RMB2(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB2);
}
inline void RMB3(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB3);
}
inline void RMB4(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB4);
}
inline void RMB5(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB5);
}
inline void RMB6(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB6);
}
inline void RMB7(Cpu* c) {
	EnqueueZpThenStep(c, &Step_RMB7);
}

// --- BBSx: Branch if Bit Set (0-7) ---

inline void BBS0(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x01) != 0);
	});
}
inline void BBS1(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x02) != 0);
	});
}
inline void BBS2(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x04) != 0);
	});
}
inline void BBS3(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x08) != 0);
	});
}
inline void BBS4(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x10) != 0);
	});
}
inline void BBS5(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x20) != 0);
	});
}
inline void BBS6(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x40) != 0);
	});
}
inline void BBS7(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x80) != 0);
	});
}

// --- BBRx: Branch if Bit Reset (0-7) ---

inline void BBR0(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x01) == 0);
	});
}
inline void BBR1(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x02) == 0);
	});
}
inline void BBR2(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x04) == 0);
	});
}
inline void BBR3(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x08) == 0);
	});
}
inline void BBR4(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x10) == 0);
	});
}
inline void BBR5(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x20) == 0);
	});
}
inline void BBR6(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x40) == 0);
	});
}
inline void BBR7(Cpu* c) {
	EnqueueZpThenStep(c, [](Cpu* cpu) {
		DoBitBranch(cpu, (cpu->_tVal & 0x80) == 0);
	});
}

} // namespace W65C02Impl::BitManipOps
