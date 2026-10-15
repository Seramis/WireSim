#pragma once
#include "../../../../Simulator/Platform.hpp"
#include <stdexcept>
#include <vector>

// Atmel AT28C256: a 32K x 8 paged-parallel EEPROM, JEDEC byte-wide pinout
// (A0-A14 address, 8-bit I/O data bus, /CE, /OE, /WE). Real hardware also
// supports byte/page writes, software data protection, and DATA
// polling/toggle-bit write-completion signalling -- see this file's .md for
// why none of that is modeled. This class only models the read path:
// contents are supplied once at construction (the image this chip was
// programmed with before being placed in a machine) and are immutable for
// the device's lifetime, matching how an EEPROM like this is actually used
// once programmed -- as a ROM.
class AT28C256 final : public Sim::Device {
  public:
	static constexpr int kAddressBits = 15; // A0-A14 -> 32,768 bytes
	static constexpr int kSize = 1 << kAddressBits;

	struct AT28C256Pins {
		Sim::Bus A; // A0-A14
		Sim::Bus D; // I/O0-I/O7
		Sim::Pin NotCE;
		Sim::Pin NotOE;
		Sim::Pin NotWE; // Present for pinout fidelity only; see class comment -- writes are not modeled.
	};
	AT28C256Pins Pins;

	// `contents` is the chip's full 32,768-byte image. Fewer bytes are
	// accepted and padded with 0xFF (an erased EEPROM cell's read value);
	// more than kSize is a programming error, not a runtime condition.
	AT28C256(Sim::Platform* p, std::vector<uint8_t> contents) :
		Pins{
			Sim::Bus::New(p, kAddressBits, "AT28C256.A"),
			Sim::Bus::New(p, 8, "AT28C256.D"),
			Sim::Pin::New(p, "AT28C256.NotCE"),
			Sim::Pin::New(p, "AT28C256.NotOE"),
			Sim::Pin::New(p, "AT28C256.NotWE")
		},
		_diagnostics(p->GetDiagnostics()), _data(std::move(contents)) {
		if (_data.size() > static_cast<size_t>(kSize))
			throw std::invalid_argument("AT28C256: contents exceed 32768 bytes");
		_data.resize(kSize, 0xFF);

		Pins.NotCE.AddTrigger(this, Ctrl::Select);
		Pins.NotOE.AddTrigger(this, Ctrl::Select);
		Pins.NotWE.AddTrigger(this, Ctrl::Select);
		Pins.A.AddTrigger(this, Ctrl::Select);
	}

	void Logic(Sim::TriggerMask) {
		const auto isWriteAttempt = Pins.NotCE.IsLow() && Pins.NotWE.IsLow();
		if (isWriteAttempt && !_lastWriteAttempt) {
			_diagnostics.Println(
				"AT28C256 write attempt ignored (contents are fixed at construction in this simulator)"
			);
		}
		_lastWriteAttempt = isWriteAttempt;

		// Real read condition per the datasheet: /CE and /OE low, /WE high.
		// A write pulse (above) already implies /OE high per the datasheet's
		// own write-cycle definition, so no extra exclusion is needed here.
		if (Pins.NotCE.IsLow() && Pins.NotOE.IsLow()) {
			const auto addr = Pins.A.Read<int>();
			Pins.D.Write(_data[addr]);
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
