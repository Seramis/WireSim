#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::TransferOps {

// --- Internal Helper ---
inline void Transfer(Cpu* c, uint8_t from, uint8_t& to) {
	Addressing::ReadCycle(c, c->PC, [c, from, &to](uint8_t) {
		to = from;
		c->UpdateFlags(to);
	});
}

// --- Accumulator Transfers ---
inline void TAX(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Transfer(c, c->A, c->X);
	});
}

inline void TAY(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Transfer(c, c->A, c->Y);
	});
}

inline void TXA(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Transfer(c, c->X, c->A);
	});
}

inline void TYA(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Transfer(c, c->Y, c->A);
	});
}

// --- Stack Pointer Transfers ---
inline void TSX(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Transfer(c, c->S, c->X);
	});
}

inline void TXS(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		// TXS is unique: It does NOT affect any flags.
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->S = c->X;
		});
	});
}
} // namespace W65C02Impl::TransferOps
