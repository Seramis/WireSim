#pragma once
#include "Addressing.hpp"
#include "W65C02Impl.hpp"

namespace W65C02Impl::AluOps {

// --- Internal Arithmetic Logic ---
inline void DoADC(Cpu* c, uint8_t operand) {
	if (c->GetFlag(Flags::Decimal)) {
		int carry = c->GetFlag(Flags::Carry) ? 1 : 0;
		int low = (c->A & 0x0F) + (operand & 0x0F) + carry;
		if (low > 0x09) low += 0x06;

		int high = (c->A >> 4) + (operand >> 4) + (low > 0x0F ? 1 : 0);
		c->SetFlag(Flags::Overflow, (~(c->A ^ operand) & (c->A ^ (high << 4)) & 0x80) != 0);

		if (high > 0x09) high += 0x06;
		c->SetFlag(Flags::Carry, high > 0x0F);

		c->A = static_cast<uint8_t>((high << 4) | (low & 0x0F));
		c->UpdateFlags(c->A); // Zero flag set based on decimal result
	} else {
		int carry = c->GetFlag(Flags::Carry) ? 1 : 0;
		int sum = static_cast<int>(c->A) + static_cast<int>(operand) + carry;
		c->SetFlag(Flags::Overflow, (~(c->A ^ operand) & (c->A ^ sum) & 0x80) != 0);
		c->A = static_cast<uint8_t>(sum);
		c->SetFlag(Flags::Carry, sum > 0xFF);
		c->UpdateFlags(c->A);
	}
}

inline void DoSBC(Cpu* c, uint8_t operand) {
	if (c->GetFlag(Flags::Decimal)) {
		int borrow = c->GetFlag(Flags::Carry) ? 0 : 1;
		int low = (c->A & 0x0F) - (operand & 0x0F) - borrow;
		if (low < 0) low -= 0x06;

		int high = (c->A >> 4) - (operand >> 4) - (low < 0 ? 1 : 0);

		// 65C02: Overflow and Negative flags are set based on the binary result
		int binarySum = static_cast<int>(c->A) - static_cast<int>(operand) - borrow;
		c->SetFlag(Flags::Overflow, ((c->A ^ operand) & (c->A ^ binarySum) & 0x80) != 0);

		if (high < 0) high -= 0x06;
		c->A = static_cast<uint8_t>((high << 4) | (low & 0x0F));

		c->SetFlag(Flags::Carry, binarySum >= 0);
		c->UpdateFlags(c->A);
	} else {
		int borrow = c->GetFlag(Flags::Carry) ? 0 : 1;
		int diff = static_cast<int>(c->A) - static_cast<int>(operand) - borrow;
		c->SetFlag(Flags::Overflow, ((c->A ^ operand) & (c->A ^ diff) & 0x80) != 0);
		c->SetFlag(Flags::Carry, diff >= 0);
		c->A = static_cast<uint8_t>(diff);
		c->UpdateFlags(c->A);
	}
}

inline void DoCMP(Cpu* c, uint8_t reg, uint8_t operand) {
	int res = static_cast<int>(reg) - static_cast<int>(operand);
	c->SetFlag(Flags::Carry, reg >= operand);
	c->UpdateFlags(static_cast<uint8_t>(res));
}

inline void UseCMP_A(Cpu* c, uint8_t data) {
	DoCMP(c, c->A, data);
}

// --- Micro-step Helpers ---
inline void Step_ADC_Mem(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		DoADC(c, data);
	});
}

inline void Step_SBC_Mem(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		DoSBC(c, data);
	});
}

inline void Step_CMP_Mem(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		DoCMP(c, c->A, data);
	});
}

inline void Step_CPX_Mem(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		DoCMP(c, c->X, data);
	});
}

inline void Step_CPY_Mem(Cpu* c) {
	Addressing::ReadCycle(c, c->_tAddr, [c](uint8_t data) {
		DoCMP(c, c->Y, data);
	});
}

/**
 * W65C02S: decimal-mode ADC/SBC take one additional cycle for the BCD
 * correction pass (Table 7-1, p.29: "Flags after decimal operation ...
 * One additional cycle."). Binary-mode ADC/SBC, and every other opcode, are
 * unaffected. Must be called before any addressing-mode steps are enqueued
 * so the extra cycle lands immediately after the opcode fetch.
 */
inline void MaybeExtraDecimalCycle(Cpu* c) {
	if (c->GetFlag(Flags::Decimal)) c->EnqueueFront(&Addressing::DummyReadPC);
}

// --- Opcodes ---
// ADC
inline void ADC_Imm(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			DoADC(c, data);
			c->PC++;
		});
	});
}
inline void ADC_Zp(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_ADC_Mem);
}
inline void ADC_ZpX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_ADC_Mem);
}
inline void ADC_Abs(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_ADC_Mem);
}
inline void ADC_AbsX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<DoADC>);
}
inline void ADC_AbsY(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<DoADC>);
}
inline void ADC_Ind(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_ADC_Mem);
}
inline void ADC_IndX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_ADC_Mem);
}
inline void ADC_IndY(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<DoADC>);
}

// SBC
inline void SBC_Imm(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			DoSBC(c, data);
			c->PC++;
		});
	});
}
inline void SBC_Zp(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_SBC_Mem);
}
inline void SBC_ZpX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_SBC_Mem);
}
inline void SBC_Abs(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_SBC_Mem);
}
inline void SBC_AbsX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<DoSBC>);
}
inline void SBC_AbsY(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<DoSBC>);
}
inline void SBC_Ind(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_SBC_Mem);
}
inline void SBC_IndX(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_SBC_Mem);
}
inline void SBC_IndY(Cpu* cpu) {
	MaybeExtraDecimalCycle(cpu);
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<DoSBC>);
}

// CMP
inline void CMP_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			DoCMP(c, c->A, data);
			c->PC++;
		});
	});
}
inline void CMP_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_CMP_Mem);
}
inline void CMP_ZpX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Step_CMP_Mem);
}
inline void CMP_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_CMP_Mem);
}
inline void CMP_AbsX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadX<UseCMP_A>);
}
inline void CMP_AbsY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseCMP_A>);
}
inline void CMP_Ind(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpInd);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_CMP_Mem);
}
inline void CMP_IndX(Cpu* cpu) {
	cpu->Enqueue(&Addressing::ZpX);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Step_CMP_Mem);
}
inline void CMP_IndY(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Ind_Ptr);
	cpu->Enqueue(&Addressing::Ind_FetchLow);
	cpu->Enqueue(&Addressing::Ind_FetchHigh);
	cpu->Enqueue(&Addressing::IndexedReadY<UseCMP_A>);
}

// CPX
inline void CPX_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			DoCMP(c, c->X, data);
			c->PC++;
		});
	});
}
inline void CPX_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_CPX_Mem);
}
inline void CPX_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_CPX_Mem);
}

// CPY
inline void CPY_Imm(Cpu* cpu) {
	cpu->Enqueue([](Cpu* c) {
		Addressing::ReadCycle(c, c->PC, [c](uint8_t data) {
			DoCMP(c, c->Y, data);
			c->PC++;
		});
	});
}
inline void CPY_Zp(Cpu* cpu) {
	cpu->Enqueue(&Addressing::Zp);
	cpu->Enqueue(&Step_CPY_Mem);
}
inline void CPY_Abs(Cpu* cpu) {
	cpu->Enqueue(&Addressing::AbsLow);
	cpu->Enqueue(&Addressing::AbsHigh);
	cpu->Enqueue(&Step_CPY_Mem);
}
} // namespace W65C02Impl::AluOps
