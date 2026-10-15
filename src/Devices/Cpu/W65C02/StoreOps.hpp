#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::StoreOps {

// --- Internal Helper ---
inline void Store(Cpu* c, uint8_t value) {
	Addressing::WriteCycle(c, c->_tAddr, value);
}

// --- STA (Store Accumulator) ---
inline void STA_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::FixX);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::FixY);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

inline void STA_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::FixY);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->A);
	});
}

// --- STX (Store X Register) ---
inline void STX_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->X);
	});
}

inline void STX_ZpY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpY);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->X);
	});
}

inline void STX_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->X);
	});
}

// --- STY (Store Y Register) ---
inline void STY_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->Y);
	});
}

inline void STY_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->Y);
	});
}

inline void STY_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, c->Y);
	});
}

// --- STZ (Store Zero - 65C02) ---
inline void STZ_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue([](Cpu* c) {
		Store(c, 0x00);
	});
}

inline void STZ_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue([](Cpu* c) {
		Store(c, 0x00);
	});
}

inline void STZ_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue([](Cpu* c) {
		Store(c, 0x00);
	});
}

inline void STZ_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::FixX);
	cpu->Enqueue([](Cpu* c) {
		Store(c, 0x00);
	});
}
} // namespace W65C02Impl::StoreOps
