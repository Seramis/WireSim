#pragma once
#include "Addressing.hpp"
#include "StackOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::JumpOps {

// --- Internal Helpers ---
inline void Step_LoadL(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t low) {
		c->_tAddrLow = low;
	});
}

inline void Step_LoadH(Cpu* c) {
	Addressing::ReadCycle(c, static_cast<uint16_t>(c->_tAddr + 1), [c](uint8_t high) {
		c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
	});
}

inline void Step_Branch_Decision(Cpu* c) {
	Addressing::ReadCycle(c, c->PC, [c](uint8_t offset) {
		c->PC++;

		// Logic Check:
		// _tVal == 2: Always (BRA)
		// _tVal == 1: Branch if flag is set
		// _tVal == 0: Branch if flag is clear
		bool conditionMet = (c->_tVal == 2) || ((c->_tVal == 1) == c->GetFlag(static_cast<Flags>(c->_tAddrLow)));

		if (conditionMet) {
			int8_t relative = static_cast<int8_t>(offset);
			c->_tAddr = static_cast<uint16_t>(c->PC + relative);

			c->Enqueue([](Cpu* cpu) {
				Addressing::ReadCycle(cpu, cpu->PC, [cpu](uint8_t) {
					// Apply low byte change immediately
					if ((cpu->PC & 0xFF00) == (cpu->_tAddr & 0xFF00)) cpu->PC = cpu->_tAddr;
				});
			});

			if ((c->PC & 0xFF00) != (c->_tAddr & 0xFF00)) {
				c->Enqueue([](Cpu* cpu) {
					Addressing::ReadCycle(cpu, cpu->_tAddr, [cpu](uint8_t) { // Fix high byte cycle
						cpu->PC = cpu->_tAddr;
					});
				});
			}
		}
	});
}

inline void BranchIf(Cpu* cpu, Flags flag, bool state) {
	cpu->_tVal = state ? 1 : 0;
	cpu->_tAddrLow = static_cast<uint8_t>(flag);
	cpu->Enqueue(&Step_Branch_Decision);
}

// --- Jumps ---
inline void JMP_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t high) {
			c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
		});
	});
}

inline void JMP_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	// W65C02S: fixes the NMOS JMP ($xxFF) page-wrap bug by computing the
	// pointer's full 16-bit +1 instead of wrapping the low byte within the
	// page (JumpOps::Step_LoadH already does a full 16-bit add). That carry
	// computation costs one extra internal cycle versus NMOS's free 8-bit
	// increment: Table 4-1 (p.20) row 5 gives NMOS=5, W65C02S=6.
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue(&Step_LoadL);
	cpu->Enqueue(&Step_LoadH);
}

inline void JMP_AbsIndX(Cpu* cpu) {
	// Flat 6 cycles always: Table 4-1 (p.20) row 2 carries no page-boundary
	// note (unlike rows 3/4), so FixX's own fixup cycle already lands on the
	// correct pointer address with no second, crossing-dependent cycle.
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::FixX);
	cpu->Enqueue(&Addressing::AbsX_Ind_FetchLow);
	cpu->Enqueue(&Addressing::AbsX_Ind_FetchHigh);
}

// --- Subroutines ---
inline void JSR(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::DummyReadPC); // Internal cycle
	cpu->Enqueue([](Cpu* c) {
		StackOps::Push(c, static_cast<uint8_t>(c->PC >> 8));
	});
	cpu->Enqueue([](Cpu* c) {
		StackOps::Push(c, static_cast<uint8_t>(c->PC & 0xFF));
	});
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t high) {
			c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
		});
	});
}

inline void RTS(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&StackOps::IncStack);
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t low) {
			c->_tAddrLow = low;
			c->S++;
		});
	});
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t high) {
			c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
		});
	});
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->PC++;
		});
	});
}

// --- Branches ---
inline void BRA(Cpu* cpu) {
	cpu->_tVal = 2;
	cpu->Enqueue(&Step_Branch_Decision);
}

inline void BNE(Cpu* cpu) {
	BranchIf(cpu, Flags::Zero, false);
}
inline void BEQ(Cpu* cpu) {
	BranchIf(cpu, Flags::Zero, true);
}
inline void BCC(Cpu* cpu) {
	BranchIf(cpu, Flags::Carry, false);
}
inline void BCS(Cpu* cpu) {
	BranchIf(cpu, Flags::Carry, true);
}
inline void BPL(Cpu* cpu) {
	BranchIf(cpu, Flags::Negative, false);
}
inline void BMI(Cpu* cpu) {
	BranchIf(cpu, Flags::Negative, true);
}
inline void BVC(Cpu* cpu) {
	BranchIf(cpu, Flags::Overflow, false);
}
inline void BVS(Cpu* cpu) {
	BranchIf(cpu, Flags::Overflow, true);
}

} // namespace W65C02Impl::JumpOps
