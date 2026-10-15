#pragma once
#include "../../Simulator/Platform.hpp"

// Address-decode glue logic (the 74HC00 network on the breadboard). Owns the
// chip-select / strobe pins of the SRAM, EEPROM, ACIA and VIA; see
// specs/specification.md section 3.1 for the decode equations. The ACIA and VIA
// qualify their own accesses with PHI2 and RWB, so for those two the chipset
// only drives the chip select; the SRAM and EEPROM have no clock input, so
// their /OE and /WE are gated with CLK here.
class BenEaterChipset final : public Sim::Device {
  public:
	struct ChipsetPins {
		Sim::Bus A;
		Sim::Pin CLK, CpuRWB;

		Sim::Pin SramCEB, SramOEB, SramWEB;
		Sim::Pin RomCEB, RomOEB;
		Sim::Pin AciaCS0;
		Sim::Pin ViaCS1;

		ChipsetPins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 16, "Chipset.A")), CLK(Sim::Pin::New(p, "Chipset.CLK")),
			CpuRWB(Sim::Pin::New(p, "Chipset.CpuRWB")), SramCEB(Sim::Pin::New(p, "Sram.CEB")),
			SramOEB(Sim::Pin::New(p, "Sram.OEB")), SramWEB(Sim::Pin::New(p, "Sram.WEB")),
			RomCEB(Sim::Pin::New(p, "Rom.CEB")), RomOEB(Sim::Pin::New(p, "Rom.OEB")),
			AciaCS0(Sim::Pin::New(p, "Acia.CS0")), ViaCS1(Sim::Pin::New(p, "Via.CS1")) {}
	};

	ChipsetPins Pins;

	BenEaterChipset(Sim::Platform* p) : Pins(p) {
		Pins.CLK.AddTrigger(this);
		Pins.CpuRWB.AddTrigger(this);
		Pins.A.AddTrigger(this);
	}

	void Logic(const Sim::TriggerMask) {
		const auto addr = Pins.A.Read<uint16_t>();
		const auto strobeActive = Pins.CLK.IsHigh();
		const auto isRead = Pins.CpuRWB.IsHigh();

		const Target target = DecodeTarget(addr);
		if (!_targetKnown || target != _lastTarget) {
			DeselectAll();
			_lastTarget = target;
			_targetKnown = true;
		}

		const auto readStrobe = Sim::LogicState::ActiveLow(isRead && strobeActive);
		const auto writeStrobe = Sim::LogicState::ActiveLow(!isRead && strobeActive);

		switch (target) {
			case Target::Sram:
				Pins.SramCEB.Write(Sim::LogicState::L);
				Pins.SramOEB.Write(readStrobe);
				Pins.SramWEB.Write(writeStrobe);
				break;
			case Target::Rom:
				Pins.RomCEB.Write(Sim::LogicState::L);
				Pins.RomOEB.Write(readStrobe);
				break;
			case Target::Acia: Pins.AciaCS0.Write(Sim::LogicState::H); break;
			case Target::Via: Pins.ViaCS1.Write(Sim::LogicState::H); break;
			case Target::None: break;
		}
	}

  private:
	// Which chip the address bus currently selects. The overwhelming majority
	// of bus cycles hit the SRAM or EEPROM, so only the selected target's pins
	// are redriven each call; a full deselect runs only when the selection
	// itself changes.
	enum class Target : uint8_t {
		Sram,
		Rom,
		Acia,
		Via,
		None
	};

	static constexpr uint16_t kRomBase = 0x8000;
	static constexpr uint16_t kSramLimit = 0x4000; // SRAM answers $0000-$3FFF only
	static constexpr uint16_t kAciaBase = 0x5000; // $5000-$500F
	static constexpr uint16_t kViaBase = 0x6000;  // $6000-$600F
	static constexpr uint16_t kWindowMask = 0xFFF0;

	Target _lastTarget = Target::Sram;
	bool _targetKnown = false;

	static Target DecodeTarget(uint16_t addr) {
		if (addr & kRomBase) return Target::Rom;
		if ((addr & kWindowMask) == kAciaBase) return Target::Acia;
		if ((addr & kWindowMask) == kViaBase) return Target::Via;
		if (addr < kSramLimit) return Target::Sram;
		return Target::None; // $4000-$7FFF outside the ACIA and VIA windows: nothing drives the bus
	}

	void DeselectAll() {
		Pins.SramCEB.Write(Sim::LogicState::H);
		Pins.SramOEB.Write(Sim::LogicState::H);
		Pins.SramWEB.Write(Sim::LogicState::H);
		Pins.RomCEB.Write(Sim::LogicState::H);
		Pins.RomOEB.Write(Sim::LogicState::H);
		Pins.AciaCS0.Write(Sim::LogicState::L);
		Pins.ViaCS1.Write(Sim::LogicState::L);
	}
};
