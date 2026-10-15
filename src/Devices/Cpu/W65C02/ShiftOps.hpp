#pragma once
#include "Addressing.hpp"
#include "CommonOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::ShiftOps {

// --- Internal RMW Helpers ---
// See CommonOps::RMW: the W65C02S performs two reads then one write for all
// memory RMW instructions, not the NMOS one-read-two-write shape.

inline void Step_Mem_ASL(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->SetFlag(Flags::Carry, (original & 0x80) != 0);
		cpu->_tVal = static_cast<uint8_t>(original << 1);
		cpu->UpdateFlags(cpu->_tVal);
	});
}
inline void Step_Mem_LSR(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->SetFlag(Flags::Carry, (original & 0x01) != 0);
		cpu->_tVal = static_cast<uint8_t>(original >> 1);
		cpu->UpdateFlags(cpu->_tVal);
	});
}
inline void Step_Mem_ROL(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		uint8_t oldCarry = cpu->GetFlag(Flags::Carry) ? 1 : 0;
		cpu->SetFlag(Flags::Carry, (original & 0x80) != 0);
		cpu->_tVal = static_cast<uint8_t>((original << 1) | oldCarry);
		cpu->UpdateFlags(cpu->_tVal);
	});
}
inline void Step_Mem_ROR(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		uint8_t oldCarry = cpu->GetFlag(Flags::Carry) ? 0x80 : 0;
		cpu->SetFlag(Flags::Carry, (original & 0x01) != 0);
		cpu->_tVal = static_cast<uint8_t>((original >> 1) | oldCarry);
		cpu->UpdateFlags(cpu->_tVal);
	});
}

// Value-consumer forms for abs,X (see IncDecOps::UseINC for the rationale).
inline void UseASL(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_ASL);
}
inline void UseLSR(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_LSR);
}
inline void UseROL(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_ROL);
}
inline void UseROR(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_ROR);
}

// --- ASL: Arithmetic Shift Left ---

inline void ASL_Acc(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Carry, (c->A & 0x80) != 0);
			c->A <<= 1;
			c->UpdateFlags(c->A);
		});
	});
}

inline void ASL_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ASL);
}
inline void ASL_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ASL);
}
inline void ASL_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ASL);
}
inline void ASL_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseASL>);
}

// --- LSR: Logical Shift Right ---

inline void LSR_Acc(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Carry, (c->A & 0x01) != 0);
			c->A >>= 1;
			c->UpdateFlags(c->A);
		});
	});
}

inline void LSR_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_LSR);
}
inline void LSR_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_LSR);
}
inline void LSR_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_LSR);
}
inline void LSR_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseLSR>);
}

// --- ROL: Rotate Left ---

inline void ROL_Acc(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			uint8_t oldCarry = c->GetFlag(Flags::Carry) ? 1 : 0;
			c->SetFlag(Flags::Carry, (c->A & 0x80) != 0);
			c->A = static_cast<uint8_t>((c->A << 1) | oldCarry);
			c->UpdateFlags(c->A);
		});
	});
}

inline void ROL_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROL);
}
inline void ROL_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROL);
}
inline void ROL_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROL);
}
inline void ROL_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseROL>);
}

// --- ROR: Rotate Right ---

inline void ROR_Acc(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			uint8_t oldCarry = c->GetFlag(Flags::Carry) ? 0x80 : 0;
			c->SetFlag(Flags::Carry, (c->A & 0x01) != 0);
			c->A = static_cast<uint8_t>((c->A >> 1) | oldCarry);
			c->UpdateFlags(c->A);
		});
	});
}

inline void ROR_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROR);
}
inline void ROR_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROR);
}
inline void ROR_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_ROR);
}
inline void ROR_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseROR>);
}
} // namespace W65C02Impl::ShiftOps
