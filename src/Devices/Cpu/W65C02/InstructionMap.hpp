#include "W65C02Impl.hpp"
#include <array>

#include "AluOps.hpp"
#include "BitManipOps.hpp"
#include "FlagOps.hpp"
#include "IncDecOps.hpp"
#include "JumpOps.hpp"
#include "LoadOps.hpp"
#include "LogicOps.hpp"
#include "ShiftOps.hpp"
#include "StackOps.hpp"
#include "StoreOps.hpp"
#include "SystemOps.hpp"
#include "TransferOps.hpp"

namespace W65C02Impl {
namespace Config {
// Define the function outside so the compiler sees the class is "complete"
static constexpr std::array<W65C02Impl::Cpu::MicroOp, 256> InitializeOpcodeTable() {
	std::array<Cpu::MicroOp, 256> table{};
	table.fill(&SystemOps::BRK);

	// ORA
	table[0x09] = &LogicOps::ORA;
	table[0x05] = &LogicOps::ORA_Zp;
	table[0x15] = &LogicOps::ORA_ZpX;
	table[0x0D] = &LogicOps::ORA_Abs;
	table[0x1D] = &LogicOps::ORA_AbsX;
	table[0x19] = &LogicOps::ORA_AbsY;
	table[0x01] = &LogicOps::ORA_IndX;
	table[0x11] = &LogicOps::ORA_IndY;
	table[0x12] = &LogicOps::ORA_Ind; // 65C02 ORA (ZP)

	// AND
	table[0x29] = &LogicOps::AND;
	table[0x25] = &LogicOps::AND_Zp;
	table[0x35] = &LogicOps::AND_ZpX;
	table[0x2D] = &LogicOps::AND_Abs;
	table[0x3D] = &LogicOps::AND_AbsX;
	table[0x39] = &LogicOps::AND_AbsY;
	table[0x21] = &LogicOps::AND_IndX;
	table[0x31] = &LogicOps::AND_IndY;
	table[0x32] = &LogicOps::AND_Ind; // 65C02 AND (ZP)

	// EOR
	table[0x49] = &LogicOps::EOR;
	table[0x45] = &LogicOps::EOR_Zp;
	table[0x55] = &LogicOps::EOR_ZpX;
	table[0x4D] = &LogicOps::EOR_Abs;
	table[0x5D] = &LogicOps::EOR_AbsX;
	table[0x59] = &LogicOps::EOR_AbsY;
	table[0x41] = &LogicOps::EOR_IndX;
	table[0x51] = &LogicOps::EOR_IndY;
	table[0x52] = &LogicOps::EOR_Ind; // 65C02 EOR (ZP)

	// ADC
	table[0x69] = &AluOps::ADC_Imm;
	table[0x65] = &AluOps::ADC_Zp;
	table[0x75] = &AluOps::ADC_ZpX;
	table[0x6D] = &AluOps::ADC_Abs;
	table[0x7D] = &AluOps::ADC_AbsX;
	table[0x79] = &AluOps::ADC_AbsY;
	table[0x61] = &AluOps::ADC_IndX;
	table[0x71] = &AluOps::ADC_IndY;
	table[0x72] = &AluOps::ADC_Ind; // 65C02 ADC (ZP)

	// SBC
	table[0xE9] = &AluOps::SBC_Imm;
	table[0xE5] = &AluOps::SBC_Zp;
	table[0xF5] = &AluOps::SBC_ZpX;
	table[0xED] = &AluOps::SBC_Abs;
	table[0xFD] = &AluOps::SBC_AbsX;
	table[0xF9] = &AluOps::SBC_AbsY;
	table[0xE1] = &AluOps::SBC_IndX;
	table[0xF1] = &AluOps::SBC_IndY;
	table[0xF2] = &AluOps::SBC_Ind; // 65C02 SBC (ZP)

	// CMP
	table[0xC9] = &AluOps::CMP_Imm;
	table[0xC5] = &AluOps::CMP_Zp;
	table[0xD5] = &AluOps::CMP_ZpX;
	table[0xCD] = &AluOps::CMP_Abs;
	table[0xDD] = &AluOps::CMP_AbsX;
	table[0xD9] = &AluOps::CMP_AbsY;
	table[0xC1] = &AluOps::CMP_IndX;
	table[0xD1] = &AluOps::CMP_IndY;
	table[0xD2] = &AluOps::CMP_Ind; // 65C02 CMP (ZP)

	// CPX
	table[0xE0] = &AluOps::CPX_Imm;
	table[0xE4] = &AluOps::CPX_Zp;
	table[0xEC] = &AluOps::CPX_Abs;

	// CPY
	table[0xC0] = &AluOps::CPY_Imm;
	table[0xC4] = &AluOps::CPY_Zp;
	table[0xCC] = &AluOps::CPY_Abs;

	// BIT
	table[0x24] = &LogicOps::BIT_Zp;
	table[0x2C] = &LogicOps::BIT_Abs;
	table[0x34] = &LogicOps::BIT_ZpX;
	table[0x3C] = &LogicOps::BIT_AbsX;
	table[0x89] = &LogicOps::BIT_Imm;

	// TSB / TRB
	table[0x04] = &LogicOps::TSB_Zp;
	table[0x0C] = &LogicOps::TSB_Abs;
	table[0x14] = &LogicOps::TRB_Zp;
	table[0x1C] = &LogicOps::TRB_Abs;

	// ASL
	table[0x0A] = &ShiftOps::ASL_Acc;
	table[0x06] = &ShiftOps::ASL_Zp;
	table[0x16] = &ShiftOps::ASL_ZpX;
	table[0x0E] = &ShiftOps::ASL_Abs;
	table[0x1E] = &ShiftOps::ASL_AbsX;

	// LSR
	table[0x4A] = &ShiftOps::LSR_Acc;
	table[0x46] = &ShiftOps::LSR_Zp;
	table[0x56] = &ShiftOps::LSR_ZpX;
	table[0x4E] = &ShiftOps::LSR_Abs;
	table[0x5E] = &ShiftOps::LSR_AbsX;

	// ROL
	table[0x2A] = &ShiftOps::ROL_Acc;
	table[0x26] = &ShiftOps::ROL_Zp;
	table[0x36] = &ShiftOps::ROL_ZpX;
	table[0x2E] = &ShiftOps::ROL_Abs;
	table[0x3E] = &ShiftOps::ROL_AbsX;

	// ROR
	table[0x6A] = &ShiftOps::ROR_Acc;
	table[0x66] = &ShiftOps::ROR_Zp;
	table[0x76] = &ShiftOps::ROR_ZpX;
	table[0x6E] = &ShiftOps::ROR_Abs;
	table[0x7E] = &ShiftOps::ROR_AbsX;

	// INC / DEC Acc
	table[0x1A] = &IncDecOps::INC;
	table[0x3A] = &IncDecOps::DEC;

	// INX, INY, DEX, DEY
	table[0xE8] = &IncDecOps::INX;
	table[0xC8] = &IncDecOps::INY;
	table[0xCA] = &IncDecOps::DEX;
	table[0x88] = &IncDecOps::DEY;

	// INC / DEC Mem
	table[0xE6] = &IncDecOps::INC_Zp;
	table[0xF6] = &IncDecOps::INC_ZpX;
	table[0xEE] = &IncDecOps::INC_Abs;
	table[0xFE] = &IncDecOps::INC_AbsX;
	table[0xC6] = &IncDecOps::DEC_Zp;
	table[0xD6] = &IncDecOps::DEC_ZpX;
	table[0xCE] = &IncDecOps::DEC_Abs;
	table[0xDE] = &IncDecOps::DEC_AbsX;

	// LDA
	table[0xA9] = &LoadOps::LDA_Imm;
	table[0xA5] = &LoadOps::LDA_Zp;
	table[0xB5] = &LoadOps::LDA_ZpX;
	table[0xAD] = &LoadOps::LDA_Abs;
	table[0xBD] = &LoadOps::LDA_AbsX;
	table[0xB9] = &LoadOps::LDA_AbsY;
	table[0xA1] = &LoadOps::LDA_IndX;
	table[0xB1] = &LoadOps::LDA_IndY;
	table[0xB2] = &LoadOps::LDA_Ind;

	// LDX
	table[0xA2] = &LoadOps::LDX_Imm;
	table[0xA6] = &LoadOps::LDX_Zp;
	table[0xB6] = &LoadOps::LDX_ZpY;
	table[0xAE] = &LoadOps::LDX_Abs;
	table[0xBE] = &LoadOps::LDX_AbsY;

	// LDY
	table[0xA0] = &LoadOps::LDY_Imm;
	table[0xA4] = &LoadOps::LDY_Zp;
	table[0xB4] = &LoadOps::LDY_ZpX;
	table[0xAC] = &LoadOps::LDY_Abs;
	table[0xBC] = &LoadOps::LDY_AbsX;

	// STA
	table[0x85] = &StoreOps::STA_Zp;
	table[0x95] = &StoreOps::STA_ZpX;
	table[0x8D] = &StoreOps::STA_Abs;
	table[0x9D] = &StoreOps::STA_AbsX;
	table[0x99] = &StoreOps::STA_AbsY;
	table[0x81] = &StoreOps::STA_IndX;
	table[0x91] = &StoreOps::STA_IndY;
	table[0x92] = &StoreOps::STA_Ind;

	// STX / STY
	table[0x86] = &StoreOps::STX_Zp;
	table[0x96] = &StoreOps::STX_ZpY;
	table[0x8E] = &StoreOps::STX_Abs;
	table[0x84] = &StoreOps::STY_Zp;
	table[0x94] = &StoreOps::STY_ZpX;
	table[0x8C] = &StoreOps::STY_Abs;

	// STZ
	table[0x64] = &StoreOps::STZ_Zp;
	table[0x74] = &StoreOps::STZ_ZpX;
	table[0x9C] = &StoreOps::STZ_Abs;
	table[0x9E] = &StoreOps::STZ_AbsX;

	// TAX, TAY, TXA, TYA, TSX, TXS
	table[0xAA] = &TransferOps::TAX;
	table[0xA8] = &TransferOps::TAY;
	table[0x8A] = &TransferOps::TXA;
	table[0x98] = &TransferOps::TYA;
	table[0xBA] = &TransferOps::TSX;
	table[0x9A] = &TransferOps::TXS;

	// Jumps
	table[0x4C] = &JumpOps::JMP_Abs;
	table[0x6C] = &JumpOps::JMP_Ind;
	table[0x7C] = &JumpOps::JMP_AbsIndX;
	table[0x20] = &JumpOps::JSR;
	table[0x60] = &JumpOps::RTS;

	// Branches
	table[0x80] = &JumpOps::BRA;
	table[0xD0] = &JumpOps::BNE;
	table[0xF0] = &JumpOps::BEQ;
	table[0x90] = &JumpOps::BCC;
	table[0xB0] = &JumpOps::BCS;
	table[0x10] = &JumpOps::BPL;
	table[0x30] = &JumpOps::BMI;
	table[0x50] = &JumpOps::BVC;
	table[0x70] = &JumpOps::BVS;

	// Flags
	table[0x18] = &FlagOps::CLC;
	table[0xD8] = &FlagOps::CLD;
	table[0x58] = &FlagOps::CLI;
	table[0xB8] = &FlagOps::CLV;
	table[0x38] = &FlagOps::SEC;
	table[0xF8] = &FlagOps::SED;
	table[0x78] = &FlagOps::SEI;

	// Stack
	table[0x48] = &StackOps::PHA;
	table[0x08] = &StackOps::PHP;
	table[0xDA] = &StackOps::PHX;
	table[0x5A] = &StackOps::PHY;
	table[0x68] = &StackOps::PLA;
	table[0x28] = &StackOps::PLP;
	table[0xFA] = &StackOps::PLX;
	table[0x7A] = &StackOps::PLY;

	// System
	table[0x00] = &SystemOps::BRK;
	table[0x40] = &SystemOps::RTI;
	table[0xCB] = &SystemOps::WAI;
	table[0xDB] = &SystemOps::STP;
	table[0xEA] = &SystemOps::NOP;

	// W65C02S reserved opcodes: all execute as NOPs (docs/w65c02s.pdf Table 7-1).
	table[0x02] = &SystemOps::NOP_Imm;
	table[0x22] = &SystemOps::NOP_Imm;
	table[0x42] = &SystemOps::NOP_Imm;
	table[0x62] = &SystemOps::NOP_Imm;
	table[0x82] = &SystemOps::NOP_Imm;
	table[0xC2] = &SystemOps::NOP_Imm;
	table[0xE2] = &SystemOps::NOP_Imm;

	table[0x03] = &SystemOps::NOP1;
	table[0x0B] = &SystemOps::NOP1;
	table[0x13] = &SystemOps::NOP1;
	table[0x1B] = &SystemOps::NOP1;
	table[0x23] = &SystemOps::NOP1;
	table[0x2B] = &SystemOps::NOP1;
	table[0x33] = &SystemOps::NOP1;
	table[0x3B] = &SystemOps::NOP1;
	table[0x43] = &SystemOps::NOP1;
	table[0x4B] = &SystemOps::NOP1;
	table[0x53] = &SystemOps::NOP1;
	table[0x5B] = &SystemOps::NOP1;
	table[0x63] = &SystemOps::NOP1;
	table[0x6B] = &SystemOps::NOP1;
	table[0x73] = &SystemOps::NOP1;
	table[0x7B] = &SystemOps::NOP1;
	table[0x83] = &SystemOps::NOP1;
	table[0x8B] = &SystemOps::NOP1;
	table[0x93] = &SystemOps::NOP1;
	table[0x9B] = &SystemOps::NOP1;
	table[0xA3] = &SystemOps::NOP1;
	table[0xAB] = &SystemOps::NOP1;
	table[0xB3] = &SystemOps::NOP1;
	table[0xBB] = &SystemOps::NOP1;
	table[0xE3] = &SystemOps::NOP1;
	table[0xEB] = &SystemOps::NOP1;
	table[0xF3] = &SystemOps::NOP1;
	table[0xFB] = &SystemOps::NOP1;

	table[0x44] = &SystemOps::NOP_Zp;

	table[0x54] = &SystemOps::NOP_ZpX;
	table[0xD4] = &SystemOps::NOP_ZpX;
	table[0xF4] = &SystemOps::NOP_ZpX;

	table[0xDC] = &SystemOps::NOP_Abs;
	table[0xFC] = &SystemOps::NOP_Abs;

	table[0x5C] = &SystemOps::NOP_5C;

	// 65C02 Bit Manipulation
	table[0x07] = &BitManipOps::RMB0;
	table[0x17] = &BitManipOps::RMB1;
	table[0x27] = &BitManipOps::RMB2;
	table[0x37] = &BitManipOps::RMB3;
	table[0x47] = &BitManipOps::RMB4;
	table[0x57] = &BitManipOps::RMB5;
	table[0x67] = &BitManipOps::RMB6;
	table[0x77] = &BitManipOps::RMB7;

	table[0x87] = &BitManipOps::SMB0;
	table[0x97] = &BitManipOps::SMB1;
	table[0xA7] = &BitManipOps::SMB2;
	table[0xB7] = &BitManipOps::SMB3;
	table[0xC7] = &BitManipOps::SMB4;
	table[0xD7] = &BitManipOps::SMB5;
	table[0xE7] = &BitManipOps::SMB6;
	table[0xF7] = &BitManipOps::SMB7;

	table[0x0F] = &BitManipOps::BBR0;
	table[0x1F] = &BitManipOps::BBR1;
	table[0x2F] = &BitManipOps::BBR2;
	table[0x3F] = &BitManipOps::BBR3;
	table[0x4F] = &BitManipOps::BBR4;
	table[0x5F] = &BitManipOps::BBR5;
	table[0x6F] = &BitManipOps::BBR6;
	table[0x7F] = &BitManipOps::BBR7;

	table[0x8F] = &BitManipOps::BBS0;
	table[0x9F] = &BitManipOps::BBS1;
	table[0xAF] = &BitManipOps::BBS2;
	table[0xBF] = &BitManipOps::BBS3;
	table[0xCF] = &BitManipOps::BBS4;
	table[0xDF] = &BitManipOps::BBS5;
	table[0xEF] = &BitManipOps::BBS6;
	table[0xFF] = &BitManipOps::BBS7;

	return table;
}
} // namespace Config
} // namespace W65C02Impl