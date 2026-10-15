#pragma once
#include "Addressing.hpp"
#include "StackOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::SystemOps {

// --- Shared Internal Micro-steps ---

inline void FetchVectorL_Generic(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t low) {
		c->_tAddrLow = low;
	});
}

inline void FetchVectorH_Generic(Cpu* c) {
	Addressing::ReadCycle(c, static_cast<uint16_t>(c->_tAddr + 1), [c](uint8_t high) {
		c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
	});
}

inline void FetchVectorL_RESET(Cpu* c) {
	Addressing::ReadCycle(c, 0xFFFC, [c](uint8_t low) {
		c->_tAddrLow = low;
	});
}

inline void FetchVectorH_RESET(Cpu* c) {
	Addressing::ReadCycle(c, 0xFFFD, [c](uint8_t high) {
		c->PC = static_cast<uint16_t>((high << 8) | c->_tAddrLow);
	});
}

// --- Interrupt Logic ---

inline void InterruptSequence(Cpu* cpu, uint16_t vector, bool isBrk) {
	cpu->_tVal = isBrk ? 1 : 0;
	cpu->_tAddr = vector;

	// Cycle 2: Dummy read
	cpu->Enqueue(&Addressing::DummyReadPC);

	// Cycle 3: Push PCH
	cpu->Enqueue([](Cpu* c) {
		StackOps::Push(c, static_cast<uint8_t>(c->PC >> 8));
	});

	// Cycle 4: Push PCL
	cpu->Enqueue([](Cpu* c) {
		StackOps::Push(c, static_cast<uint8_t>(c->PC & 0xFF));
	});

	// Cycle 5: Push P
	cpu->Enqueue([](Cpu* c) {
		uint8_t pToPush = c->P | 0x20;		// Bit 5 always high
		if (c->_tVal == 1) pToPush |= 0x10; // Break bit if software BRK
		else pToPush &= ~0x10;

		Addressing::WriteCycle(c, static_cast<uint16_t>(0x0100 | c->S), pToPush, [c] {
			c->S--;
			c->SetFlag(Flags::InterruptDisable, true);
			c->SetFlag(Flags::Decimal, false); // 65C02: Clear D on interrupt
		});
	});
	cpu->Enqueue(&FetchVectorL_Generic);
	cpu->Enqueue(&FetchVectorH_Generic);
}

// --- Opcodes & Hardware Handlers ---

inline void NOP(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC);
	});
}

// --- W65C02S Reserved Opcodes (all NOPs; see docs/w65c02s.pdf Table 7-1) ---

// $02,$22,$42,$62,$82,$C2,$E2: 2 bytes, 2 cycles.
inline void NOP_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->PC++;
		});
	});
}

// $x3, $0B-$BB (low nibble B), $EB, $FB: 1 byte, 1 cycle (opcode fetch only).
inline void NOP1(Cpu*) {}

// $44: 2 bytes, 3 cycles.
inline void NOP_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Addressing::DummyFixCycle);
}

// $54,$D4,$F4: 2 bytes, 4 cycles.
inline void NOP_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue([](Cpu* c) {
		uint16_t indexed = static_cast<uint16_t>((c->_tAddr + c->X) & 0x00FF);
		Addressing::ReadCycle(c, indexed);
	});
}

// $DC,$FC: 3 bytes, 4 cycles.
inline void NOP_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::DummyFixCycle);
}

// $5C: 3 bytes, 8 cycles.
inline void NOP_5C(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue(&Addressing::DummyFixCycle);
	cpu->Enqueue(&Addressing::DummyFixCycle);
}

inline void WAI(Cpu* cpu) {
	// The simulator has no halt/interrupt wait state machine, so decode and
	// execute as a single-cycle NOP for validation purposes.
	NOP(cpu);
}

inline void STP(Cpu* cpu) {
	// Similarly, STOP is treated as a no-op in this digital model so the
	// opcode can be decoded and validated without a dedicated power-down state.
	NOP(cpu);
}

inline void BRK(Cpu* cpu) {
	cpu->PC++; // 65C02: BRK is 2 bytes
	InterruptSequence(cpu, 0xFFFE, true);
}

inline void HandleReset(Cpu* cpu) {
	cpu->PC = 0x0000;
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&FetchVectorL_RESET);
	cpu->Enqueue(&FetchVectorH_RESET);
}

inline void HandleNMI(Cpu* cpu) {
	InterruptSequence(cpu, 0xFFFA, false);
}
inline void HandleIRQ(Cpu* cpu) {
	InterruptSequence(cpu, 0xFFFE, false);
}

inline void RTI(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&StackOps::IncStack);

	// Cycle 4: Pull P
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t p) {
			c->P = (p & 0xEF) | 0x20; // Ignore B, keep Reserved high
			c->S++;
		});
	});

	// Cycle 5: Pull PCL
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t pcl) {
			c->_tAddrLow = pcl;
			c->S++;
		});
	});

	// Cycle 6: Pull PCH
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t pch) {
			c->PC = static_cast<uint16_t>((pch << 8) | c->_tAddrLow);
		});
	});
}
} // namespace W65C02Impl::SystemOps
