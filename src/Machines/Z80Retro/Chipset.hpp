#pragma once
#include "../../Simulator/Inspect.hpp"
#include "../../Simulator/Platform.hpp"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The glue logic of the Z80-Retro CPU board, drawn from its schematic:
//   - I/O port decode (74HC138): IORQ low, M1 high, A7 low; A6-A4 pick one of
//     eight 16-port windows. A3-A0 are not decoded by it (peripherals take A1
//     and A0 themselves), so every window repeats across its 16 ports.
//       $00 GP input buffer (74LS244) drives the data bus
//       $10 GP output latch (74HC374): SD card pins, printer strobe, RAM bank
//       $20 printer data latch (74HC374)
//       $30 SIO chip enable   $40 CTC chip enable
//       $70 flash disable
//     Ports $50 and $60 are decoded by nothing, and so is everything with A7 high.
//   - The two 374 latches clock on the rising edge of their select, so an IN or
//     OUT in their window latches whatever is on the data bus (pulled high when
//     nothing drives it). The 244 has no read strobe either: it drives the bus
//     throughout any I/O cycle in its window.
//   - Shadow flash select (cross-coupled NAND latch): reset maps the flash over
//     every memory cycle while writes still reach the SRAM and SRAM reads are
//     off; any I/O cycle in the $70 window swaps to SRAM only. Only reset undoes it.
//   - SRAM bank select (four OR gates): A15 high forces SRAM address lines
//     A15-A18 high (bank 15); A15 low passes GP output bits 4-7 (banks 0-14,
//     bank 15 again aliases the high half).
// See AGENTS.md. Class is named per machine to avoid clashing with another
// machine's `Chipset` in one translation unit.
class Z80RetroChipset final : public Sim::Device, public Sim::IRegisterInspectable {
  public:
	struct ChipsetPins {
		Sim::Bus A, D;
		Sim::Pin NotMREQ, NotIORQ, NotRD, NotM1, NotRESET;

		Sim::Bus GpIn;	   // 74LS244 inputs: printer status, user jumper, SD card detect and MISO
		Sim::Bus GpOut;	   // 74HC374 outputs: SD MOSI/CLK/SSEL, printer strobe, bank bits
		Sim::Bus PrnData;  // 74HC374 outputs: printer data
		Sim::Bus SramBank; // SRAM A15-A18

		Sim::Pin FlashNotCE, SramNotOE, SioNotCE, CtcNotCE;

		ChipsetPins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 16, "Chipset.A")), D(Sim::Bus::New(p, 8, "Chipset.D")),
			NotMREQ(Sim::Pin::New(p, "Chipset.NotMREQ")), NotIORQ(Sim::Pin::New(p, "Chipset.NotIORQ")),
			NotRD(Sim::Pin::New(p, "Chipset.NotRD")), NotM1(Sim::Pin::New(p, "Chipset.NotM1")),
			NotRESET(Sim::Pin::New(p, "Chipset.NotRESET")), GpIn(Sim::Bus::New(p, 8, "Chipset.GpIn")),
			GpOut(Sim::Bus::New(p, 8, "Chipset.GpOut")), PrnData(Sim::Bus::New(p, 8, "Chipset.PrnData")),
			SramBank(Sim::Bus::New(p, 4, "Chipset.SramBank")), FlashNotCE(Sim::Pin::New(p, "Flash.NotCE")),
			SramNotOE(Sim::Pin::New(p, "Sram.NotOE")), SioNotCE(Sim::Pin::New(p, "Sio.NotCE")),
			CtcNotCE(Sim::Pin::New(p, "Ctc.NotCE")) {}
	};

	ChipsetPins Pins;

	Z80RetroChipset(Sim::Platform* p) : Pins(p) {
		Pins.A.AddTrigger(this, Trig::Bus);
		Pins.NotMREQ.AddTrigger(this, Trig::Bus);
		Pins.NotIORQ.AddTrigger(this, Trig::Bus);
		Pins.NotRD.AddTrigger(this, Trig::Bus);
		Pins.NotM1.AddTrigger(this, Trig::Bus);
		Pins.NotRESET.AddTrigger(this, Trig::Bus);
		Pins.GpIn.AddTrigger(this, Trig::Bus);
	}

	void Logic(const Sim::TriggerMask) {
		const auto addr = Pins.A.Read<uint16_t>();
		const bool ioCycle = Pins.NotIORQ.IsLow() && !Pins.NotM1.IsLow() && !(addr & kIoWindowDisableBit);
		const auto port = ioCycle ? static_cast<Port>((addr >> 4) & 7) : Port::None;

		// Shadow flash latch: reset wins, otherwise the $70 window swaps to SRAM.
		if (Pins.NotRESET.IsLow()) _flashMapped = true;
		else if (port == Port::FlashDisable) _flashMapped = false;

		// The 374s take the bus when their select ends.
		if (_gpOutSelected && port != Port::GpOut) {
			_gpOut = Pins.D.Read<uint8_t>();
			Pins.GpOut.Write(_gpOut);
		}
		_gpOutSelected = port == Port::GpOut;
		if (_prnDataSelected && port != Port::PrnData) {
			_prnData = Pins.D.Read<uint8_t>();
			Pins.PrnData.Write(_prnData);
		}
		_prnDataSelected = port == Port::PrnData;

		// The 244 drives the bus while selected.
		if (port == Port::GpIn) {
			Pins.D.Write(Pins.GpIn.Read<uint8_t>());
			_drivingData = true;
		} else if (_drivingData) {
			Pins.D.Clear();
			_drivingData = false;
		}

		// Flash is chip-enabled by any memory cycle while mapped (not by address); its /OE and /WE
		// come straight from the CPU. SRAM /CE and /WE are likewise the CPU's, so writes always
		// land, but its /OE is off while the flash answers.
		Pins.FlashNotCE.Write(Sim::LogicState::ActiveLow(_flashMapped && Pins.NotMREQ.IsLow()));
		Pins.SramNotOE.Write(Sim::LogicState::ActiveLow(!_flashMapped && Pins.NotRD.IsLow()));
		Pins.SramBank.Write((addr & kHighBankBit) ? 0xF : _gpOut >> kBankShift);

		Pins.SioNotCE.Write(Sim::LogicState::ActiveLow(port == Port::Sio));
		Pins.CtcNotCE.Write(Sim::LogicState::ActiveLow(port == Port::Ctc));
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return {{"FlashMapped", _flashMapped}, {"GpOut", _gpOut}, {"PrnData", _prnData}};
	}

  private:
	enum class Trig : uint8_t {
		Bus = 0
	};

	enum class Port : uint8_t {
		GpIn = 0,
		GpOut = 1,
		PrnData = 2,
		Sio = 3,
		Ctc = 4,
		FlashDisable = 7,
		None = 8
	};

	static constexpr uint16_t kIoWindowDisableBit = 0x0080; // A7 must be low for any window
	static constexpr uint16_t kHighBankBit = 0x8000;		// A15: SRAM bank 15, always
	static constexpr int kBankShift = 4;					// GP output bits 4-7 select the low bank

	bool _flashMapped = true;
	uint8_t _gpOut = 0;
	uint8_t _prnData = 0;
	bool _gpOutSelected = false;
	bool _prnDataSelected = false;
	bool _drivingData = false;
};
