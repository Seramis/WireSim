#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::LoadOps {

// --- Internal Helpers ---

/**
 * Internal generic load from an address into a target register.
 */
inline void Load(Cpu* c, uint16_t addr, uint8_t& reg) {
	Addressing::ReadCycle(c, addr, [c, &reg](uint8_t data) {
		reg = data;
		c->UpdateFlags(data);
	});
}

/**
 * Internal generic immediate load (fetches from PC).
 */
inline void LoadImm(Cpu* c, uint8_t& reg) {
	Addressing::ReadCycle(c, c->PC, [c, &reg](uint8_t data) {
		reg = data;
		c->UpdateFlags(data);
		c->PC++;
	});
}

// Value-consumer form, usable as the `use` callback passed to indexed addressing.
inline void UseLoadA(Cpu* c, uint8_t data) {
	c->A = data;
	c->UpdateFlags(data);
}
inline void UseLoadX(Cpu* c, uint8_t data) {
	c->X = data;
	c->UpdateFlags(data);
}
inline void UseLoadY(Cpu* c, uint8_t data) {
	c->Y = data;
	c->UpdateFlags(data);
}

// --- LDA (Load Accumulator) ---
inline void LDA_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		LoadImm(c, c->A);
	});
}
inline void LDA_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->A);
	});
}
inline void LDA_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->A);
	});
}
inline void LDA_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->A);
	});
}
inline void LDA_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseLoadA>);
}
inline void LDA_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseLoadA>);
}

inline void LDA_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->A);
	});
}

inline void LDA_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->A);
	});
}

inline void LDA_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseLoadA>);
}

// --- LDX (Load X Register) ---
inline void LDX_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		LoadImm(c, c->X);
	});
}
inline void LDX_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->X);
	});
}
inline void LDX_ZpY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpY);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->X);
	});
}
inline void LDX_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->X);
	});
}
inline void LDX_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseLoadX>);
}

// --- LDY (Load Y Register) ---
inline void LDY_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		LoadImm(c, c->Y);
	});
}
inline void LDY_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->Y);
	});
}
inline void LDY_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->Y);
	});
}
inline void LDY_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Load(c, c->_tAddr, c->Y);
	});
}
inline void LDY_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseLoadY>);
}

} // namespace W65C02Impl::LoadOps
