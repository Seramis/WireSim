#include "W65C02Impl.hpp"
#include "InstructionMap.hpp"
#include <cassert>
#include <print>

namespace W65C02Impl {
const std::array<Cpu::MicroOp, 256> Cpu::_opTable = Config::InitializeOpcodeTable();

Cpu::Cpu() : pins(), _microBuffer{} {
	S = 0xFF;
	P = 0x34; // Interrupt disabled, Break/Reserved bits set
}

void Cpu::Next() {
	if (!pins.NMIBin) {
		if (!_nmiEdgeDetected) {
			_nmiOccurred = true;
			_nmiEdgeDetected = true;
		}
	} else {
		_nmiEdgeDetected = false;
	}

	if (_lastClock == pins.PHI2in) return;
	_lastClock = pins.PHI2in;

	if (!pins.PHI2in) {
		// PHI2 falling: sample the completed transfer, then prepare the next PHI1.
		// W65C02S: RDY stops the processor during PHI2 on write cycles too (NMOS
		// ignores RDY during writes). Datasheet Table 7-1, p.30.
		if (pins.RDYin) {
			if (_currentStep) {
				const auto completedStep = _currentStep;
				_currentStep = nullptr;
				completedStep(this);
			}

			if (_queuedSteps == 0) DetermineNextInstruction();

			if (_queuedSteps != 0) {
				_currentStep = _microBuffer[_head];
				_head = (_head + 1) % _microBuffer.size();
				--_queuedSteps;
				_currentStep(this);
			}
		}
	}

	pins.PHI2out = pins.PHI2in;
	pins.PHI1out = !pins.PHI2in;
}

void Cpu::DetermineNextInstruction() {
	if (!pins.RESBin) {
		SystemOps::HandleReset(this);
	} else if (_nmiOccurred) {
		_nmiOccurred = false;
		SystemOps::HandleNMI(this);
	} else if (!pins.IRQBin && !GetFlag(Flags::InterruptDisable)) {
		SystemOps::HandleIRQ(this);
	} else {
		Enqueue(&Cpu::FetchOpCode);
	}
}

void Cpu::FetchOpCode(Cpu* c) {
	if (c->AddressPhase(c->PC)) return;

	uint8_t opcode;
	if (c->LatchPhase(opcode)) {
		c->PC++;
		// _opTable is fully populated (Config::InitializeOpcodeTable fills every
		// slot with SystemOps::BRK before overriding the implemented ones), so
		// this is never null.
		c->_opTable[opcode](c);
	}
}

void Cpu::Enqueue(MicroOp step) {
	// The opcode table's longest micro-op sequence (NOP_5C) is 7 steps against
	// this 16-slot buffer, so overflow can't happen; assert (compiled out under
	// NDEBUG) instead of a Release-mode throw to avoid duplicating a
	// never-taken exception path at every one of this function's many inlined
	// call sites (see PERFORMANCE.md finding #1).
	assert(_queuedSteps != _microBuffer.size() && "65C02 micro-op queue overflow");
	_microBuffer[_tail] = step;
	_tail = (_tail + 1) % _microBuffer.size();
	++_queuedSteps;
}

void Cpu::EnqueueFront(MicroOp step) {
	assert(_queuedSteps != _microBuffer.size() && "65C02 micro-op queue overflow");
	_head = (_head + _microBuffer.size() - 1) % _microBuffer.size();
	_microBuffer[_head] = step;
	++_queuedSteps;
}

bool Cpu::AddressPhase(uint16_t addr, bool rw) {
	if (!_currentStep) return false;
	pins.Aout = addr;
	pins.RWBout = rw;
	return true;
}

bool Cpu::LatchPhase(uint8_t& data) {
	data = pins.Din;
	return _currentStep == nullptr;
}

void Cpu::UpdateFlags(uint8_t val) {
	SetFlag(Flags::Zero, val == 0);
	SetFlag(Flags::Negative, (val & 0x80) != 0);
}

void Cpu::SetFlag(Flags flag, bool state) {
	if (state) P |= static_cast<uint8_t>(flag);
	else P &= ~static_cast<uint8_t>(flag);
}

bool Cpu::GetFlag(Flags flag) {
	return (P & static_cast<uint8_t>(flag)) != 0;
}

void Cpu::DumpState(uint64_t cpuCycles, uint64_t halfCycles) const {
	std::println(
		"CPU state c={} h={} PC={:04x} A={:02x} X={:02x} Y={:02x} S={:02x} P={:08b} bus={:04x} {} Din={:02x} "
		"Dout={:02x}",
		cpuCycles,
		halfCycles,
		PC,
		A,
		X,
		Y,
		S,
		P,
		pins.Aout,
		pins.RWBout ? "R" : "W",
		pins.Din,
		pins.Dout
	);
}

// PC/A/X/Y are whatever the real 6502 leaves them as -- uninitialized until
// the first reset sequence and subsequent execution actually sets them, so a
// value read before any cycle has run isn't meaningful. S/P are the two
// exceptions: the constructor sets them deterministically (0xFF/0x34) -- see
// W65C02.devtest, which is the only pair this interface can assert pre-reset.
std::vector<std::pair<std::string, uint64_t>> Cpu::DumpRegisters() const {
	return {
		{"PC", PC},
		{"A", A},
		{"X", X},
		{"Y", Y},
		{"S", S},
		{"P", P},
	};
}
} // namespace W65C02Impl