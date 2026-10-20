#pragma once
#include "../../../../Simulator/Inspect.hpp"
#include "../../../../Simulator/Platform.hpp"
#include <stdexcept>
#include <vector>

// SST SST39SF010A: a 128K x 8 sector-erase flash, JEDEC byte-wide pinout
// (A0-A16 address, 8-bit DQ data bus, /CE, /OE, /WE). Real hardware also
// supports byte program, sector and chip erase and software ID through
// command sequences -- see this file's .md. This class only models the read
// path: contents are supplied once at construction (the image the chip was
// programmed with before being placed in a machine) and stay fixed.
class SST39SF010 final : public Sim::Device, public Sim::IMemoryInspectable {
  public:
	static constexpr int kAddressBits = 17; // A0-A16 -> 131,072 bytes
	static constexpr int kSize = 1 << kAddressBits;

	struct SST39SF010Pins {
		Sim::Bus A; // A0-A16
		Sim::Bus D; // DQ0-DQ7
		Sim::Pin NotCE;
		Sim::Pin NotOE;
		Sim::Pin NotWE; // Present for pinout fidelity only; see class comment -- writes are not modeled.
	};
	SST39SF010Pins Pins;

	// `contents` is the chip's full 131,072-byte image. Fewer bytes are accepted
	// and padded with 0xFF (an erased cell's read value); more than kSize is a
	// programming error, not a runtime condition.
	SST39SF010(Sim::Platform* p, std::vector<uint8_t> contents) :
		Pins{
			Sim::Bus::New(p, kAddressBits, "SST39SF010.A"),
			Sim::Bus::New(p, 8, "SST39SF010.D"),
			Sim::Pin::New(p, "SST39SF010.NotCE"),
			Sim::Pin::New(p, "SST39SF010.NotOE"),
			Sim::Pin::New(p, "SST39SF010.NotWE")
		},
		_diagnostics(p->GetDiagnostics()), _data(std::move(contents)) {
		if (_data.size() > static_cast<size_t>(kSize))
			throw std::invalid_argument("SST39SF010: contents exceed 131072 bytes");
		_data.resize(kSize, 0xFF);

		Pins.NotCE.AddTrigger(this, Ctrl::Select);
		Pins.NotOE.AddTrigger(this, Ctrl::Select);
		Pins.NotWE.AddTrigger(this, Ctrl::Select);
		Pins.A.AddTrigger(this, Ctrl::Select);
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

	void Logic(Sim::TriggerMask) {
		const auto isWriteAttempt = Pins.NotCE.IsLow() && Pins.NotWE.IsLow();
		if (isWriteAttempt && !_lastWriteAttempt) {
			_diagnostics.Println("SST39SF010 write ignored (commands are not modeled)");
		}
		_lastWriteAttempt = isWriteAttempt;

		// Read cycle: /CE and /OE low, /WE high. A write pulse needs /OE high, so a
		// bus cycle that pulses /WE while /OE is low is not a read.
		if (Pins.NotCE.IsLow() && Pins.NotOE.IsLow() && !Pins.NotWE.IsLow()) {
			Pins.D.Write(_data[Pins.A.Read<int>()]);
		} else {
			Pins.D.Clear(); // High-Z
		}
	}

  private:
	enum class Ctrl : uint8_t {
		Select = 0
	};

	Sim::Diagnostics& _diagnostics;
	std::vector<uint8_t> _data;
	bool _lastWriteAttempt = false;
};
