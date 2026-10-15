#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::FlagOps {

// --- Clear Flags ---
inline void CLC(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Carry, false);
		});
	});
}

inline void CLD(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Decimal, false);
		});
	});
}

inline void CLI(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::InterruptDisable, false);
		});
	});
}

inline void CLV(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Overflow, false);
		});
	});
}

// --- Set Flags ---
inline void SEC(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Carry, true);
		});
	});
}

inline void SED(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::Decimal, true);
		});
	});
}

inline void SEI(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t) {
			c->SetFlag(Flags::InterruptDisable, true);
		});
	});
}
} // namespace W65C02Impl::FlagOps
