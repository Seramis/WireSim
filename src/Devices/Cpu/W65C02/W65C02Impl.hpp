#pragma once
#include "Flags.hpp"
#include "Pins.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace W65C02Impl {
class Cpu {
  public:
	using MicroOp = void (*)(Cpu*);
	// Hardware Registers
	uint16_t PC;
	uint8_t A, X, Y, S, P;
	Pins pins;

	// Internal Scratchpads
	uint16_t _tAddr;
	uint8_t _tAddrLow;
	uint8_t _tVal;
	void (*_tContinuation)(Cpu*, uint8_t) = nullptr;

	Cpu();

	void Next();
	void Enqueue(MicroOp step);
	void EnqueueFront(MicroOp step);

	bool AddressPhase(uint16_t addr, bool rw = true);
	bool LatchPhase(uint8_t& data);

	void UpdateFlags(uint8_t val);
	void SetFlag(Flags flag, bool state);
	bool GetFlag(Flags flag);

	void DumpState(uint64_t cpuCycles, uint64_t halfCycles) const;
	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const;

  private:
	static const std::array<MicroOp, 256> _opTable;

	std::array<MicroOp, 16> _microBuffer{};
	std::size_t _head = 0;
	std::size_t _tail = 0;
	std::size_t _queuedSteps = 0;

	MicroOp _currentStep = nullptr;

	bool _nmiEdgeDetected = false;
	bool _nmiOccurred = false;
	bool _lastClock = false;
	void DetermineNextInstruction();
	static void FetchOpCode(Cpu* c);
};
} // namespace W65C02Impl