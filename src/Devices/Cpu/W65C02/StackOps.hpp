#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::StackOps {

// --- Internal Shared Helpers ---
inline void Push(Cpu* c, uint8_t val) {
	Addressing::WriteCycle(c, static_cast<uint16_t>(0x0100 | c->S), val, [c] {
		c->S--;
	});
}

inline void Pull(Cpu* c, uint8_t& reg) {
	Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c, &reg](uint8_t data) {
		reg = data;
		c->UpdateFlags(reg);
	});
}

inline void IncStack(Cpu* c) {
	Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t) {
		c->S++;
	});
}

// --- Push Operations ---
inline void PHA(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue([](Cpu* c) {
		Push(c, c->A);
	});
}

inline void PHP(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue([](Cpu* c) {
		Push(c, static_cast<uint8_t>(c->P | 0x30));
	});
}

inline void PHX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue([](Cpu* c) {
		Push(c, c->X);
	});
}

inline void PHY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue([](Cpu* c) {
		Push(c, c->Y);
	});
}

// --- Pull Operations ---
inline void PLA(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&IncStack);
	cpu->Enqueue([](Cpu* c) {
		Pull(c, c->A);
	});
}

inline void PLP(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&IncStack);
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, static_cast<uint16_t>(0x0100 | c->S), [c](uint8_t val) {
			// 65C02: Bit 5 is always 1, Bit 4 is ignored
			c->P = (val & 0xEF) | 0x20;
		});
	});
}

inline void PLX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&IncStack);
	cpu->Enqueue([](Cpu* c) {
		Pull(c, c->X);
	});
}

inline void PLY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::DummyReadPC);
	cpu->Enqueue(&IncStack);
	cpu->Enqueue([](Cpu* c) {
		Pull(c, c->Y);
	});
}
} // namespace W65C02Impl::StackOps
