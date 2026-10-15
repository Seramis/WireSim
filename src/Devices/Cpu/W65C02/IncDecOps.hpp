#pragma once
#include "Addressing.hpp"
#include "CommonOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::IncDecOps {

// --- Internal Read-Modify-Write Helpers ---
inline void Step_Mem_INC(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original + 1);
		cpu->UpdateFlags(cpu->_tVal);
	});
}

inline void Step_Mem_DEC(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->_tVal = static_cast<uint8_t>(original - 1);
		cpu->UpdateFlags(cpu->_tVal);
	});
}

// Value-consumer forms for abs,X. `Addressing::IndexedReadX` already implements
// the "this cycle IS the data unless the index carries" optimization, so
// routing the first RMW read through it (instead of a separate Abs_Calc
// fixup + Read_TVal) gives the correct 6/7-cycle abs,X RMW shape instead of a
// flat, one-too-many count.
inline void UseINC(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_INC);
}
inline void UseDEC(Cpu* c, uint8_t data) {
	c->_tVal = data;
	c->Enqueue(&Step_Mem_DEC);
}

// --- Register Operations (Implied) ---
inline void INC(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->A++;
			c->UpdateFlags(c->A);
		});
	});
}

inline void DEC(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->A--;
			c->UpdateFlags(c->A);
		});
	});
}

inline void INX(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->X++;
			c->UpdateFlags(c->X);
		});
	});
}

inline void DEX(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->X--;
			c->UpdateFlags(c->X);
		});
	});
}

inline void INY(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->Y++;
			c->UpdateFlags(c->Y);
		});
	});
}

inline void DEY(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->Y--;
			c->UpdateFlags(c->Y);
		});
	});
}

// --- INC: Increment Memory ---
inline void INC_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_INC);
}

inline void INC_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_INC);
}

inline void INC_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_INC);
}

inline void INC_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseINC>);
}

// --- DEC: Decrement Memory ---

inline void DEC_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_DEC);
}

inline void DEC_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_DEC);
}

inline void DEC_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_Mem_DEC);
}

inline void DEC_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseDEC>);
}
} // namespace W65C02Impl::IncDecOps
