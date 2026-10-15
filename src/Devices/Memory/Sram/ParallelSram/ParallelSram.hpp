#pragma once
#include "../../../../Simulator/Inspect.hpp"
#include "../../../../Simulator/Platform.hpp"
#include <iostream>
#include <vector>

// A generic, parametrized parallel-interface SRAM -- not modeling any one
// specific real chip's part number/timing, unlike a device that would live
// as a sibling under this same Sram/ folder for that. Trace output still
// says "SRAM" (its role), independent of this class's name.
class ParallelSram final : public Sim::Device, public Sim::IMemoryInspectable {
  public:
	struct ParallelSramPins {
		Sim::Bus A;
		Sim::Bus D;
		Sim::Pin NotCE;
		Sim::Pin NotWE;
		Sim::Pin NotOE;
	};

	ParallelSramPins Pins;

  private:
	enum class CTRL : uint8_t {
		NotCE = 0,
		NotOE = 1,
		NotWE = 2,
		Address = 3
	};

	std::vector<uint8_t> _data;

	bool _lastIsWrite = false;
	bool _lastIsRead = false;
	uint8_t _lastWriteData;
	int _lastWriteAddr;
	int _lastReadAddr = -1;
	Sim::Diagnostics& _diagnostics;

  public:
	ParallelSram(Sim::Platform* p, int addrWidth, int dataWidth) :
		Pins{
			Sim::Bus::New(p, addrWidth, "SRAM.A"),
			Sim::Bus::New(p, dataWidth, "SRAM.D"),
			Sim::Pin::New(p, "NotCE"),
			Sim::Pin::New(p, "NotWE"),
			Sim::Pin::New(p, "NotOE")
		},
		_diagnostics(p->GetDiagnostics()) {
		_data.resize(1 << addrWidth, 0);

		Pins.NotCE.AddTrigger(this, CTRL::NotCE);
		Pins.NotOE.AddTrigger(this, CTRL::NotOE);
		Pins.A.AddTrigger(this, CTRL::Address);

		Pins.NotWE.AddTrigger(this, CTRL::NotWE, false);
	}

	// Direct memory access for loading/debugging
	uint8_t& operator[](int index) {
		return _data[index];
	}

	uint32_t InspectableSize() const override {
		return static_cast<uint32_t>(_data.size());
	}
	uint8_t Peek(uint32_t address) const override {
		return _data[address];
	}
	void Poke(uint32_t address, uint8_t value) override {
		_data[address] = value;
	}

	void Logic(Sim::TriggerMask mask) {
		const auto isWrite = Pins.NotCE.IsLow() && Pins.NotWE.IsLow();
		const auto isRead = Pins.NotCE.IsLow() && Pins.NotOE.IsLow() && !isWrite;
		const auto isWriteDone = !isWrite && _lastIsWrite;

		_lastIsWrite = isWrite;

		if (isWriteDone) {
			const auto addr = _lastWriteAddr;
			_data[addr] = _lastWriteData;
			_diagnostics.Println("SRAM commit W {:04x}:{:02x}", addr, _data[addr]);
		}

		if (isWrite && mask.IsSet(CTRL::NotWE)) {
			_lastWriteAddr = Pins.A.Read<int>();
			_lastWriteData = Pins.D.Read<uint8_t>();
			_diagnostics.Println("SRAM capture W {:04x}:{:02x}", _lastWriteAddr, _lastWriteData);
		}

		if (isRead) {
			const auto addr = Pins.A.Read<int>();
			Pins.D.Write(_data[addr]);
			if (!_lastIsRead || addr != _lastReadAddr) {
				_diagnostics.Println(
					"SRAM drive R CE={} OE={} WE={} {:04x}:{:02x}",
					Pins.NotCE.IsLow(),
					Pins.NotOE.IsLow(),
					Pins.NotWE.IsLow(),
					addr,
					_data[addr]
				);
			}
			_lastIsRead = true;
			_lastReadAddr = addr;
		} else {
			Pins.D.Clear(); // High-Z
			_lastIsRead = false;
		}
	}
};
