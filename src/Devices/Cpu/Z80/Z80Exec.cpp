#include "Z80Impl.hpp"
#include <bit>

// Instruction set. Every instruction is straight-line code that `co_await`s its
// bus cycles in the order and with the T-state lengths of the Z80 CPU User
// Manual (UM008011). Opcodes are decoded by field: x = op[7:6], y = op[5:3],
// z = op[2:0], p = y[2:1], q = y[0].

namespace Z80Impl {

namespace {
constexpr uint8_t FlagsSZP = FlagS | FlagZ | FlagPV;

inline bool Parity(uint8_t v) {
	return (std::popcount(v) & 1) == 0;
}
inline uint8_t Sz(uint8_t v) {
	return static_cast<uint8_t>((v & 0x80) | (v == 0 ? FlagZ : 0));
}
} // namespace

// ---------------------------------------------------------------------------
// Registers and conditions
// ---------------------------------------------------------------------------

uint16_t Cpu::Rp(int p) const {
	switch (p) {
		case 0: return Pair(0);
		case 1: return Pair(2);
		case 2: return HlOrIndex();
		default: return sp;
	}
}

void Cpu::SetRp(int p, uint16_t v) {
	switch (p) {
		case 0: SetPair(0, v); break;
		case 1: SetPair(2, v); break;
		case 2: SetHlOrIndex(v); break;
		default: sp = v; break;
	}
}

uint16_t Cpu::Rp2(int p) const {
	return p == 3 ? static_cast<uint16_t>(r[7] << 8 | f) : Rp(p);
}

void Cpu::SetRp2(int p, uint16_t v) {
	if (p == 3) {
		r[7] = static_cast<uint8_t>(v >> 8);
		f = static_cast<uint8_t>(v);
	} else {
		SetRp(p, v);
	}
}

bool Cpu::Cond(int cc) const {
	switch (cc) {
		case 0: return !(f & FlagZ);
		case 1: return f & FlagZ;
		case 2: return !(f & FlagC);
		case 3: return f & FlagC;
		case 4: return !(f & FlagPV);
		case 5: return f & FlagPV;
		case 6: return !(f & FlagS);
		default: return f & FlagS;
	}
}

// ---------------------------------------------------------------------------
// ALU. Flags that an instruction defines are rewritten; bits 3 and 5 of F are
// not modeled and read back as 0 after any instruction that updates F.
// ---------------------------------------------------------------------------

void Cpu::SetSzp(uint8_t v) {
	f = static_cast<uint8_t>(Sz(v) | (Parity(v) ? FlagPV : 0));
}

void Cpu::Alu(int op, uint8_t v) {
	const uint8_t a = r[7];
	const unsigned c = (op == 1 || op == 3) ? (f & FlagC) : 0;
	uint8_t res;
	uint8_t flags;
	switch (op) {
		case 0:
		case 1: {
			const unsigned sum = a + v + c;
			res = static_cast<uint8_t>(sum);
			flags = static_cast<uint8_t>(
				Sz(res) | ((((a & 0xF) + (v & 0xF) + c) > 0xF) ? FlagH : 0) |
				((~(a ^ v) & (a ^ res) & 0x80) ? FlagPV : 0) | (sum > 0xFF ? FlagC : 0)
			);
			break;
		}
		case 2:
		case 3:
		case 7: {
			const unsigned diff = a - v - c;
			res = static_cast<uint8_t>(diff);
			flags = static_cast<uint8_t>(
				Sz(res) | FlagN | (((a & 0xF) < ((v & 0xF) + c)) ? FlagH : 0) |
				(((a ^ v) & (a ^ res) & 0x80) ? FlagPV : 0) | ((a < v + c) ? FlagC : 0)
			);
			break;
		}
		case 4:
			res = a & v;
			flags = static_cast<uint8_t>(Sz(res) | FlagH | (Parity(res) ? FlagPV : 0));
			break;
		case 5:
			res = a ^ v;
			flags = static_cast<uint8_t>(Sz(res) | (Parity(res) ? FlagPV : 0));
			break;
		default:
			res = a | v;
			flags = static_cast<uint8_t>(Sz(res) | (Parity(res) ? FlagPV : 0));
			break;
	}
	f = flags;
	if (op != 7) r[7] = res;
}

uint8_t Cpu::Inc8(uint8_t v) {
	const uint8_t res = static_cast<uint8_t>(v + 1);
	f = static_cast<uint8_t>((f & FlagC) | Sz(res) | ((v & 0xF) == 0xF ? FlagH : 0) | (v == 0x7F ? FlagPV : 0));
	return res;
}

uint8_t Cpu::Dec8(uint8_t v) {
	const uint8_t res = static_cast<uint8_t>(v - 1);
	f = static_cast<uint8_t>((f & FlagC) | Sz(res) | FlagN | ((v & 0xF) == 0 ? FlagH : 0) | (v == 0x80 ? FlagPV : 0));
	return res;
}

// CB-page rotate/shift selected by y: RLC RRC RL RR SLA SRA (SLL, unused) SRL.
uint8_t Cpu::Rot(int op, uint8_t v) {
	uint8_t res;
	bool carry;
	const bool cin = f & FlagC;
	switch (op) {
		case 0:
			carry = v & 0x80;
			res = static_cast<uint8_t>(v << 1 | (carry ? 1 : 0));
			break;
		case 1:
			carry = v & 1;
			res = static_cast<uint8_t>(v >> 1 | (carry ? 0x80 : 0));
			break;
		case 2:
			carry = v & 0x80;
			res = static_cast<uint8_t>(v << 1 | (cin ? 1 : 0));
			break;
		case 3:
			carry = v & 1;
			res = static_cast<uint8_t>(v >> 1 | (cin ? 0x80 : 0));
			break;
		case 4:
			carry = v & 0x80;
			res = static_cast<uint8_t>(v << 1);
			break;
		case 5:
			carry = v & 1;
			res = static_cast<uint8_t>((v >> 1) | (v & 0x80));
			break;
		default:
			carry = v & 1;
			res = static_cast<uint8_t>(v >> 1);
			break;
	}
	SetSzp(res);
	if (carry) f |= FlagC;
	return res;
}

void Cpu::Daa() {
	const uint8_t a = r[7];
	const bool n = f & FlagN;
	const bool hIn = f & FlagH;
	bool carry = f & FlagC;
	uint8_t correction = 0;
	if (hIn || (a & 0xF) > 9) correction |= 0x06;
	if (carry || a > 0x99) {
		correction |= 0x60;
		carry = true;
	}
	const uint8_t res = static_cast<uint8_t>(n ? a - correction : a + correction);
	const bool hOut = n ? (hIn && (a & 0xF) < 6) : ((a & 0xF) > 9);
	r[7] = res;
	f = static_cast<uint8_t>(
		Sz(res) | (Parity(res) ? FlagPV : 0) | (n ? FlagN : 0) | (hOut ? FlagH : 0) | (carry ? FlagC : 0)
	);
}

uint16_t Cpu::Add16(uint16_t a, uint16_t b) {
	const uint32_t sum = static_cast<uint32_t>(a) + b;
	f = static_cast<uint8_t>(
		(f & FlagsSZP) | ((((a & 0xFFF) + (b & 0xFFF)) > 0xFFF) ? FlagH : 0) | (sum > 0xFFFF ? FlagC : 0)
	);
	return static_cast<uint16_t>(sum);
}

uint16_t Cpu::Adc16(uint16_t a, uint16_t b) {
	const uint32_t c = f & FlagC;
	const uint32_t sum = static_cast<uint32_t>(a) + b + c;
	const uint16_t res = static_cast<uint16_t>(sum);
	f = static_cast<uint8_t>(
		(res & 0x8000 ? FlagS : 0) | (res == 0 ? FlagZ : 0) | ((((a & 0xFFF) + (b & 0xFFF) + c) > 0xFFF) ? FlagH : 0) |
		((~(a ^ b) & (a ^ res) & 0x8000) ? FlagPV : 0) | (sum > 0xFFFF ? FlagC : 0)
	);
	return res;
}

uint16_t Cpu::Sbc16(uint16_t a, uint16_t b) {
	const uint32_t c = f & FlagC;
	const uint32_t diff = static_cast<uint32_t>(a) - b - c;
	const uint16_t res = static_cast<uint16_t>(diff);
	f = static_cast<uint8_t>(
		(res & 0x8000 ? FlagS : 0) | (res == 0 ? FlagZ : 0) | FlagN | (((a & 0xFFF) < ((b & 0xFFF) + c)) ? FlagH : 0) |
		(((a ^ b) & (a ^ res) & 0x8000) ? FlagPV : 0) | ((a < b + c) ? FlagC : 0)
	);
	return res;
}

// ---------------------------------------------------------------------------
// Bus helpers
// ---------------------------------------------------------------------------

SubTask<void> Cpu::Push16(uint16_t v) {
	--sp;
	co_await Write(sp, static_cast<uint8_t>(v >> 8));
	--sp;
	co_await Write(sp, static_cast<uint8_t>(v));
}

SubTask<uint16_t> Cpu::Pop16() {
	const uint8_t lo = co_await Read(sp);
	++sp;
	const uint8_t hi = co_await Read(sp);
	++sp;
	co_return static_cast<uint16_t>(hi << 8 | lo);
}

SubTask<uint16_t> Cpu::Read16(uint16_t addr) {
	const uint8_t lo = co_await Read(addr);
	const uint8_t hi = co_await Read(static_cast<uint16_t>(addr + 1));
	co_return static_cast<uint16_t>(hi << 8 | lo);
}

SubTask<void> Cpu::Write16(uint16_t addr, uint16_t v) {
	co_await Write(addr, static_cast<uint8_t>(v));
	co_await Write(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(v >> 8));
}

SubTask<uint16_t> Cpu::FetchOperand16(uint8_t extraHi) {
	const uint8_t lo = co_await Read(pc);
	++pc;
	const uint8_t hi = co_await Read(pc, extraHi);
	++pc;
	co_return static_cast<uint16_t>(hi << 8 | lo);
}

// Address of the (HL) operand, or (IX+d)/(IY+d) under an index prefix. The
// indexed form reads the displacement and spends five T-states adding it.
SubTask<uint16_t> Cpu::OperandAddress() {
	if (_idx == 0) co_return Pair(4);
	const uint8_t d = co_await Read(pc);
	++pc;
	co_await Idle(5);
	co_return static_cast<uint16_t>((_idx == 1 ? ix : iy) + static_cast<int8_t>(d));
}

// ---------------------------------------------------------------------------
// Top level
// ---------------------------------------------------------------------------

Task Cpu::Program() {
	for (;;) {
		const auto kind = static_cast<Interrupt>(co_await Boundary());
		if (kind == Interrupt::Nmi) {
			co_await ServiceNmi();
			continue;
		}
		if (kind == Interrupt::Int) {
			co_await ServiceInt();
			continue;
		}
		const uint8_t op = co_await Fetch(pc);
		if (_halted) continue; // HALT repeats NOP fetches; PC stays on the next instruction
		++pc;
		_idx = 0;
		co_await Execute(op);
	}
}

SubTask<void> Cpu::ServiceNmi() {
	_halted = false;
	iff2 = iff1;
	iff1 = false;
	co_await Fetch(pc); // the byte read is ignored and PC does not advance
	Extend(1);
	co_await Push16(pc);
	pc = 0x0066;
}

SubTask<void> Cpu::ServiceInt() {
	_halted = false;
	iff1 = iff2 = false;
	const uint8_t vector = co_await IntAck(pc);
	switch (im) {
		case 0:
			// The device supplies an instruction; its first byte arrives in the
			// acknowledge cycle (PC is not advanced), the rest by normal reads.
			_idx = 0;
			co_await Execute(vector);
			break;
		case 1:
			Extend(1);
			co_await Push16(pc);
			pc = 0x0038;
			break;
		default: {
			Extend(1);
			co_await Push16(pc);
			pc = co_await Read16(static_cast<uint16_t>(i << 8 | vector));
			break;
		}
	}
}

SubTask<void> Cpu::Execute(uint8_t op) {
	while (op == 0xDD || op == 0xFD) {
		_idx = op == 0xDD ? 1 : 2;
		op = co_await Fetch(pc);
		++pc;
	}
	if (op == 0xCB) {
		if (_idx != 0) co_await ExecIndexedCB();
		else co_await ExecCB();
		co_return;
	}
	if (op == 0xED) {
		_idx = 0;
		co_await ExecED();
		co_return;
	}

	const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
	switch (x) {
		case 0:
			switch (z) {
				case 0: {
					if (y == 0) break; // NOP
					if (y == 1) {
						std::swap(r[7], rAlt[7]);
						std::swap(f, fAlt);
						break;
					}
					bool take;
					if (y == 2) {
						Extend(1); // DJNZ: five-T-state M1
						--r[0];
						take = r[0] != 0;
					} else if (y == 3) {
						take = true;
					} else {
						take = Cond(y - 4);
					}
					const uint8_t d = co_await Read(pc);
					++pc;
					if (take) {
						co_await Idle(5);
						pc = static_cast<uint16_t>(pc + static_cast<int8_t>(d));
					}
					break;
				}
				case 1:
					if (q == 0) {
						const uint16_t nn = co_await FetchOperand16();
						SetRp(p, nn);
					} else {
						co_await Idle(4);
						co_await Idle(3);
						SetHlOrIndex(Add16(HlOrIndex(), Rp(p)));
					}
					break;
				case 2:
					if (q == 0) {
						if (p == 0) {
							co_await Write(Pair(0), r[7]);
						} else if (p == 1) {
							co_await Write(Pair(2), r[7]);
						} else if (p == 2) {
							const uint16_t nn = co_await FetchOperand16();
							co_await Write16(nn, HlOrIndex());
						} else {
							const uint16_t nn = co_await FetchOperand16();
							co_await Write(nn, r[7]);
						}
					} else {
						if (p == 0) {
							r[7] = co_await Read(Pair(0));
						} else if (p == 1) {
							r[7] = co_await Read(Pair(2));
						} else if (p == 2) {
							const uint16_t nn = co_await FetchOperand16();
							const uint16_t v = co_await Read16(nn);
							SetHlOrIndex(v);
						} else {
							const uint16_t nn = co_await FetchOperand16();
							r[7] = co_await Read(nn);
						}
					}
					break;
				case 3:
					Extend(2); // INC/DEC rp: six-T-state M1
					SetRp(p, static_cast<uint16_t>(Rp(p) + (q == 0 ? 1 : -1)));
					break;
				case 4:
				case 5: {
					const bool inc = z == 4;
					if (y == 6) {
						const uint16_t ea = co_await OperandAddress();
						uint8_t v = co_await Read(ea, 1);
						v = inc ? Inc8(v) : Dec8(v);
						co_await Write(ea, v);
					} else {
						r[y] = inc ? Inc8(r[y]) : Dec8(r[y]);
					}
					break;
				}
				case 6:
					if (y != 6) {
						r[y] = co_await Read(pc);
						++pc;
					} else if (_idx == 0) {
						const uint8_t n = co_await Read(pc);
						++pc;
						co_await Write(Pair(4), n);
					} else {
						const uint8_t d = co_await Read(pc);
						++pc;
						const uint8_t n = co_await Read(pc, 2);
						++pc;
						co_await Write(static_cast<uint16_t>((_idx == 1 ? ix : iy) + static_cast<int8_t>(d)), n);
					}
					break;
				default: {
					const uint8_t a = r[7];
					switch (y) {
						case 0:
							r[7] = static_cast<uint8_t>(a << 1 | a >> 7);
							f = static_cast<uint8_t>((f & FlagsSZP) | (a >> 7));
							break;
						case 1:
							r[7] = static_cast<uint8_t>(a >> 1 | a << 7);
							f = static_cast<uint8_t>((f & FlagsSZP) | (a & 1));
							break;
						case 2:
							r[7] = static_cast<uint8_t>(a << 1 | (f & FlagC));
							f = static_cast<uint8_t>((f & FlagsSZP) | (a >> 7));
							break;
						case 3:
							r[7] = static_cast<uint8_t>(a >> 1 | (f & FlagC) << 7);
							f = static_cast<uint8_t>((f & FlagsSZP) | (a & 1));
							break;
						case 4: Daa(); break;
						case 5:
							r[7] = static_cast<uint8_t>(~a);
							f = static_cast<uint8_t>((f & (FlagsSZP | FlagC)) | FlagH | FlagN);
							break;
						case 6: f = static_cast<uint8_t>((f & FlagsSZP) | FlagC); break;
						default: f = static_cast<uint8_t>((f & FlagsSZP) | ((f & FlagC) ? FlagH : FlagC)); break;
					}
					break;
				}
			}
			break;

		case 1:
			if (y == 6 && z == 6) {
				_halted = true;
			} else if (z == 6) {
				const uint16_t ea = co_await OperandAddress();
				r[y] = co_await Read(ea);
			} else if (y == 6) {
				const uint16_t ea = co_await OperandAddress();
				co_await Write(ea, r[z]);
			} else {
				r[y] = r[z];
			}
			break;

		case 2:
			if (z == 6) {
				const uint16_t ea = co_await OperandAddress();
				const uint8_t v = co_await Read(ea);
				Alu(y, v);
			} else {
				Alu(y, r[z]);
			}
			break;

		default:
			switch (z) {
				case 0:
					Extend(1);
					if (Cond(y)) pc = co_await Pop16();
					break;
				case 1:
					if (q == 0) {
						const uint16_t v = co_await Pop16();
						SetRp2(p, v);
					} else if (p == 0) {
						pc = co_await Pop16();
					} else if (p == 1) {
						for (std::size_t k = 0; k < 6; ++k)
							std::swap(r[k], rAlt[k]);
					} else if (p == 2) {
						pc = HlOrIndex();
					} else {
						Extend(2);
						sp = HlOrIndex();
					}
					break;
				case 2: {
					const uint16_t nn = co_await FetchOperand16();
					if (Cond(y)) pc = nn;
					break;
				}
				case 3:
					switch (y) {
						case 0: pc = co_await FetchOperand16(); break;
						case 2: {
							const uint8_t n = co_await Read(pc);
							++pc;
							co_await Out(static_cast<uint16_t>(r[7] << 8 | n), r[7]);
							break;
						}
						case 3: {
							const uint8_t n = co_await Read(pc);
							++pc;
							r[7] = co_await In(static_cast<uint16_t>(r[7] << 8 | n));
							break;
						}
						case 4: {
							const uint8_t lo = co_await Read(sp);
							const uint8_t hi = co_await Read(static_cast<uint16_t>(sp + 1), 1);
							const uint16_t old = HlOrIndex();
							co_await Write(static_cast<uint16_t>(sp + 1), static_cast<uint8_t>(old >> 8));
							co_await Write(sp, static_cast<uint8_t>(old), 2);
							SetHlOrIndex(static_cast<uint16_t>(hi << 8 | lo));
							break;
						}
						case 5: {
							const uint16_t de = Pair(2);
							SetPair(2, Pair(4));
							SetPair(4, de);
							break;
						}
						case 6: iff1 = iff2 = false; break;
						default:
							iff1 = iff2 = true;
							_eiDelay = 2;
							break;
					}
					break;
				case 4: {
					const uint8_t lo = co_await Read(pc);
					++pc;
					const bool take = Cond(y);
					const uint8_t hi = co_await Read(pc, take ? 1 : 0);
					++pc;
					if (take) {
						co_await Push16(pc);
						pc = static_cast<uint16_t>(hi << 8 | lo);
					}
					break;
				}
				case 5:
					if (q == 0) {
						Extend(1);
						co_await Push16(Rp2(p));
					} else {
						// CALL nn (p == 0); DD/ED/FD were consumed above.
						const uint8_t lo = co_await Read(pc);
						++pc;
						const uint8_t hi = co_await Read(pc, 1);
						++pc;
						co_await Push16(pc);
						pc = static_cast<uint16_t>(hi << 8 | lo);
					}
					break;
				case 6: {
					const uint8_t n = co_await Read(pc);
					++pc;
					Alu(y, n);
					break;
				}
				default:
					Extend(1);
					co_await Push16(pc);
					pc = static_cast<uint16_t>(y * 8);
					break;
			}
			break;
	}
}

// CB prefix without an index register. The prefix byte is already consumed.
SubTask<void> Cpu::ExecCB() {
	const uint8_t op = co_await Fetch(pc);
	++pc;
	const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7;

	if (x == 0 && y == 6) co_return; // SLL is not part of the documented set: two-byte no-op

	if (z != 6) {
		switch (x) {
			case 0: r[z] = Rot(y, r[z]); break;
			case 1: {
				const bool zero = !(r[z] & (1 << y));
				f = static_cast<uint8_t>((f & (FlagS | FlagPV | FlagC)) | FlagH | (zero ? FlagZ : 0));
				break;
			}
			case 2: r[z] &= static_cast<uint8_t>(~(1 << y)); break;
			default: r[z] |= static_cast<uint8_t>(1 << y); break;
		}
		co_return;
	}

	const uint16_t ea = Pair(4);
	uint8_t v = co_await Read(ea, 1);
	if (x == 1) {
		const bool zero = !(v & (1 << y));
		f = static_cast<uint8_t>((f & (FlagS | FlagPV | FlagC)) | FlagH | (zero ? FlagZ : 0));
		co_return;
	}
	switch (x) {
		case 0: v = Rot(y, v); break;
		case 2: v &= static_cast<uint8_t>(~(1 << y)); break;
		default: v |= static_cast<uint8_t>(1 << y); break;
	}
	co_await Write(ea, v);
}

// DD CB d op / FD CB d op. Only the (IX+d)/(IY+d) forms are documented; the
// register field is ignored.
SubTask<void> Cpu::ExecIndexedCB() {
	const uint8_t d = co_await Read(pc);
	++pc;
	const uint8_t op = co_await Read(pc, 2);
	++pc;
	const uint8_t x = op >> 6, y = (op >> 3) & 7;
	const uint16_t ea = static_cast<uint16_t>((_idx == 1 ? ix : iy) + static_cast<int8_t>(d));

	if (x == 0 && y == 6) co_return;

	uint8_t v = co_await Read(ea, 1);
	if (x == 1) {
		const bool zero = !(v & (1 << y));
		f = static_cast<uint8_t>((f & (FlagS | FlagPV | FlagC)) | FlagH | (zero ? FlagZ : 0));
		co_return;
	}
	switch (x) {
		case 0: v = Rot(y, v); break;
		case 2: v &= static_cast<uint8_t>(~(1 << y)); break;
		default: v |= static_cast<uint8_t>(1 << y); break;
	}
	co_await Write(ea, v);
}

SubTask<void> Cpu::ExecED() {
	const uint8_t op = co_await Fetch(pc);
	++pc;
	const uint8_t x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;

	if (x == 2) {
		if (y >= 4 && z < 4) co_await ExecBlock(y, z);
		co_return;
	}
	if (x != 1) co_return; // undefined ED opcode: two-byte no-op

	switch (z) {
		case 0: {
			const uint8_t v = co_await In(Pair(0));
			if (y != 6) r[y] = v;
			f = static_cast<uint8_t>((f & FlagC) | Sz(v) | (Parity(v) ? FlagPV : 0));
			break;
		}
		case 1:
			if (y != 6) co_await Out(Pair(0), r[y]);
			break;
		case 2:
			co_await Idle(4);
			co_await Idle(3);
			SetPair(4, q == 0 ? Sbc16(Pair(4), Rp(p)) : Adc16(Pair(4), Rp(p)));
			break;
		case 3: {
			const uint16_t nn = co_await FetchOperand16();
			if (q == 0) {
				co_await Write16(nn, Rp(p));
			} else {
				const uint16_t v = co_await Read16(nn);
				SetRp(p, v);
			}
			break;
		}
		case 4:
			if (y == 0) {
				const uint8_t v = r[7];
				r[7] = 0;
				Alu(2, v);
			}
			break;
		case 5:
			if (y == 0) {
				pc = co_await Pop16();
				iff1 = iff2; // RETN
			} else if (y == 1) {
				pc = co_await Pop16(); // RETI
			}
			break;
		case 6:
			if (y == 0) im = 0;
			else if (y == 2) im = 1;
			else if (y == 3) im = 2;
			break;
		default:
			switch (y) {
				case 0:
					Extend(1);
					i = r[7];
					break;
				case 1:
					Extend(1);
					rr = r[7];
					break;
				case 2:
				case 3: {
					Extend(1);
					const uint8_t v = y == 2 ? i : rr;
					r[7] = v;
					f = static_cast<uint8_t>((f & FlagC) | Sz(v) | (iff2 ? FlagPV : 0));
					_ldAirPending = true;
					break;
				}
				case 4:
				case 5: {
					const uint16_t hl = Pair(4);
					const uint8_t m = co_await Read(hl);
					co_await Idle(4);
					const uint8_t a = r[7];
					uint8_t newM, newA;
					if (y == 4) { // RRD
						newM = static_cast<uint8_t>((a << 4) | (m >> 4));
						newA = static_cast<uint8_t>((a & 0xF0) | (m & 0x0F));
					} else { // RLD
						newM = static_cast<uint8_t>((m << 4) | (a & 0x0F));
						newA = static_cast<uint8_t>((a & 0xF0) | (m >> 4));
					}
					r[7] = newA;
					f = static_cast<uint8_t>((f & FlagC) | Sz(newA) | (Parity(newA) ? FlagPV : 0));
					co_await Write(hl, newM);
					break;
				}
				default: break;
			}
			break;
	}
}

// ED A0-A3, A8-AB, B0-B3, B8-BB. y: 4 = I, 5 = D, 6 = IR, 7 = DR; z: 0 LD, 1 CP, 2 IN, 3 OUT.
SubTask<void> Cpu::ExecBlock(uint8_t y, uint8_t z) {
	const bool dec = y & 1;
	const bool repeat = y >= 6;
	const uint16_t step = dec ? static_cast<uint16_t>(-1) : 1;

	switch (z) {
		case 0: {
			const uint8_t v = co_await Read(Pair(4));
			co_await Write(Pair(2), v, 2);
			SetPair(4, static_cast<uint16_t>(Pair(4) + step));
			SetPair(2, static_cast<uint16_t>(Pair(2) + step));
			const uint16_t bc = static_cast<uint16_t>(Pair(0) - 1);
			SetPair(0, bc);
			f = static_cast<uint8_t>((f & (FlagS | FlagZ | FlagC)) | (bc != 0 ? FlagPV : 0));
			if (repeat && bc != 0) {
				co_await Idle(5);
				pc = static_cast<uint16_t>(pc - 2);
			}
			break;
		}
		case 1: {
			const uint8_t v = co_await Read(Pair(4));
			co_await Idle(5);
			const uint8_t a = r[7];
			const uint8_t res = static_cast<uint8_t>(a - v);
			SetPair(4, static_cast<uint16_t>(Pair(4) + step));
			const uint16_t bc = static_cast<uint16_t>(Pair(0) - 1);
			SetPair(0, bc);
			f = static_cast<uint8_t>(
				(f & FlagC) | Sz(res) | FlagN | ((a & 0xF) < (v & 0xF) ? FlagH : 0) | (bc != 0 ? FlagPV : 0)
			);
			if (repeat && bc != 0 && res != 0) {
				co_await Idle(5);
				pc = static_cast<uint16_t>(pc - 2);
			}
			break;
		}
		case 2: {
			Extend(1);
			const uint8_t v = co_await In(Pair(0));
			co_await Write(Pair(4), v);
			--r[0];
			SetPair(4, static_cast<uint16_t>(Pair(4) + step));
			f = static_cast<uint8_t>((f & (FlagS | FlagH | FlagPV | FlagC)) | FlagN | (r[0] == 0 ? FlagZ : 0));
			if (repeat && r[0] != 0) {
				co_await Idle(5);
				pc = static_cast<uint16_t>(pc - 2);
			}
			break;
		}
		default: {
			Extend(1);
			const uint8_t v = co_await Read(Pair(4));
			--r[0];
			co_await Out(Pair(0), v);
			SetPair(4, static_cast<uint16_t>(Pair(4) + step));
			f = static_cast<uint8_t>((f & (FlagS | FlagH | FlagPV | FlagC)) | FlagN | (r[0] == 0 ? FlagZ : 0));
			if (repeat && r[0] != 0) {
				co_await Idle(5);
				pc = static_cast<uint16_t>(pc - 2);
			}
			break;
		}
	}
}

std::vector<std::pair<std::string, uint64_t>> Cpu::DumpRegisters() const {
	return {
		{"PC", pc},
		{"SP", sp},
		{"IX", ix},
		{"IY", iy},
		{"A", r[7]},
		{"F", f},
		{"B", r[0]},
		{"C", r[1]},
		{"D", r[2]},
		{"E", r[3]},
		{"H", r[4]},
		{"L", r[5]},
		{"A2", rAlt[7]},
		{"F2", fAlt},
		{"B2", rAlt[0]},
		{"C2", rAlt[1]},
		{"D2", rAlt[2]},
		{"E2", rAlt[3]},
		{"H2", rAlt[4]},
		{"L2", rAlt[5]},
		{"I", i},
		{"R", rr},
		{"IFF1", iff1},
		{"IFF2", iff2},
		{"IM", im},
		{"FS", (f & FlagS) != 0},
		{"FZ", (f & FlagZ) != 0},
		{"FH", (f & FlagH) != 0},
		{"FPV", (f & FlagPV) != 0},
		{"FN", (f & FlagN) != 0},
		{"FC", (f & FlagC) != 0},
	};
}

} // namespace Z80Impl
