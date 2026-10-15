#pragma once
#include "Addressing.hpp"
#include "CommonOps.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::LogicOps {

// --- ORA: Logical Inclusive OR ---
inline void ORA(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			c->A |= data;
			c->UpdateFlags(c->A);
			c->PC++;
		});
	});
}

inline void UseORA(Cpu* c, uint8_t data) {
	c->A |= data;
	c->UpdateFlags(c->A);
}

inline void Step_ORA(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		UseORA(c, data);
	});
}

inline void ORA_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_ORA);
}
inline void ORA_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_ORA);
}
inline void ORA_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_ORA);
}
inline void ORA_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseORA>);
}
inline void ORA_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseORA>);
}
inline void ORA_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_ORA);
} // 65C02 ORA (ZP)
inline void ORA_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_ORA);
}
inline void ORA_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseORA>);
}

// --- AND: Logical AND ---
inline void AND(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			c->A &= data;
			c->UpdateFlags(c->A);
			c->PC++;
		});
	});
}

inline void UseAND(Cpu* c, uint8_t data) {
	c->A &= data;
	c->UpdateFlags(c->A);
}

inline void Step_AND(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		UseAND(c, data);
	});
}

inline void AND_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_AND);
}
inline void AND_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_AND);
}
inline void AND_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_AND);
}
inline void AND_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseAND>);
}
inline void AND_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseAND>);
}
inline void AND_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_AND);
} // 65C02 AND (ZP)
inline void AND_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_AND);
}
inline void AND_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseAND>);
}

// --- EOR: Exclusive OR ---
inline void EOR(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			c->A ^= data;
			c->UpdateFlags(c->A);
			c->PC++;
		});
	});
}

inline void UseEOR(Cpu* c, uint8_t data) {
	c->A ^= data;
	c->UpdateFlags(c->A);
}

inline void Step_EOR(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		UseEOR(c, data);
	});
}

inline void EOR_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_EOR);
}
inline void EOR_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_EOR);
}
inline void EOR_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_EOR);
}
inline void EOR_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseEOR>);
}
inline void EOR_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseEOR>);
}
inline void EOR_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_EOR);
} // 65C02 EOR (ZP)
inline void EOR_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_EOR);
}
inline void EOR_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseEOR>);
}

// --- BIT: Bit Test ---
inline void BIT_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			c->SetFlag(Flags::Zero, (c->A & data) == 0);
			c->SetFlag(Flags::Negative, (data & 0x80) != 0);
			c->SetFlag(Flags::Overflow, (data & 0x40) != 0);
			c->PC++;
		});
	});
}

inline void UseBIT(Cpu* c, uint8_t data) {
	c->SetFlag(Flags::Zero, (c->A & data) == 0);
	c->SetFlag(Flags::Negative, (data & 0x80) != 0);
	c->SetFlag(Flags::Overflow, (data & 0x40) != 0);
}

inline void Step_BIT(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		UseBIT(c, data);
	});
}

inline void BIT_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_BIT);
}
inline void BIT_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_BIT);
}
inline void BIT_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_BIT);
}
inline void BIT_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseBIT>);
}

// --- 65C02 TSB / TRB (Test and Set/Reset Bits) ---
// Routed through CommonOps::RMW (two reads then one write, per the W65C02S's
// documented RMW pattern) rather than a bespoke one-read/one-write sequence.
// Zero is computed from the *original* value, before TSB/TRB's modification.

inline void Step_TSB(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->SetFlag(Flags::Zero, (cpu->A & original) == 0);
		cpu->_tVal = static_cast<uint8_t>(original | cpu->A);
	});
}

inline void Step_TRB(Cpu* c) {
	CommonOps::RMW(c, [](Cpu* cpu, uint8_t original) {
		cpu->SetFlag(Flags::Zero, (cpu->A & original) == 0);
		cpu->_tVal = static_cast<uint8_t>(original & ~cpu->A);
	});
}

inline void TSB_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_TSB);
}

inline void TRB_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_TRB);
}

inline void TSB_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_TSB);
}

inline void TRB_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&CommonOps::Read_TVal);
	cpu->Enqueue(&Step_TRB);
}
} // namespace W65C02Impl::LogicOps
