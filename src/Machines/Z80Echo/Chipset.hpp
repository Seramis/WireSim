#pragma once
#include "../../Simulator/Platform.hpp"

// Address-decode glue logic. Owns the chip-select / strobe pins of the EEPROM,
// SRAM and UART, plus the UART reset and interrupt inversions:
//   - memory space (MREQ low): A15 low selects the EEPROM, A15 high the SRAM;
//   - I/O space (IORQ low, M1 high): ports $80-$87 select the UART, A0-A2 pick
//     its register. Only A3-A7 are decoded, A8-A15 (which carry the accumulator
//     or B on IN/OUT) are ignored. IORQ with M1 low is an interrupt
//     acknowledge, not an I/O cycle, and selects nothing.
// The Z80 asserts RD / WR only inside a bus cycle, so they gate the strobes
// directly and the chipset needs no clock input.
class Z80EchoChipset final : public Sim::Device {
  public:
	struct ChipsetPins {
		Sim::Bus A;
		Sim::Pin NotMREQ, NotIORQ, NotRD, NotWR, NotM1, RSTB;

		Sim::Pin RomCEB, RomOEB;
		Sim::Pin RamCEB, RamOEB, RamWEB;

		Sim::Pin UartCS2B, UartIORB, UartIOWB, UartINT, UartRST;
		Sim::Pin NotINT;

		ChipsetPins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 16, "Chipset.A")), NotMREQ(Sim::Pin::New(p, "Chipset.NotMREQ")),
			NotIORQ(Sim::Pin::New(p, "Chipset.NotIORQ")), NotRD(Sim::Pin::New(p, "Chipset.NotRD")),
			NotWR(Sim::Pin::New(p, "Chipset.NotWR")), NotM1(Sim::Pin::New(p, "Chipset.NotM1")),
			RSTB(Sim::Pin::New(p, "Chipset.RSTB")), RomCEB(Sim::Pin::New(p, "Rom.CEB")),
			RomOEB(Sim::Pin::New(p, "Rom.OEB")), RamCEB(Sim::Pin::New(p, "Ram.CEB")),
			RamOEB(Sim::Pin::New(p, "Ram.OEB")), RamWEB(Sim::Pin::New(p, "Ram.WEB")),
			UartCS2B(Sim::Pin::New(p, "Uart.CS2B")), UartIORB(Sim::Pin::New(p, "Uart.IORB")),
			UartIOWB(Sim::Pin::New(p, "Uart.IOWB")), UartINT(Sim::Pin::New(p, "Uart.INT")),
			UartRST(Sim::Pin::New(p, "Uart.RST")), NotINT(Sim::Pin::New(p, "Chipset.NotINT")) {}
	};

	ChipsetPins Pins;

	Z80EchoChipset(Sim::Platform* p) : Pins(p) {
		Pins.A.AddTrigger(this, LogicFlags::Bus);
		Pins.NotMREQ.AddTrigger(this, LogicFlags::Bus);
		Pins.NotIORQ.AddTrigger(this, LogicFlags::Bus);
		Pins.NotRD.AddTrigger(this, LogicFlags::Bus);
		Pins.NotWR.AddTrigger(this, LogicFlags::Bus);
		Pins.NotM1.AddTrigger(this, LogicFlags::Bus);

		Pins.RSTB.AddTrigger(this, LogicFlags::Rst);
		Pins.UartINT.AddTrigger(this, LogicFlags::Int);
	}

	void Logic(const Sim::TriggerMask mask) {
		if (mask.IsSet(LogicFlags::Rst)) OnRst();
		if (mask.IsSet(LogicFlags::Int)) OnInt();
		if (mask.IsSet(LogicFlags::Bus)) OnBus();
	}

  private:
	enum class LogicFlags : uint8_t {
		Bus = 0,
		Int = 1,
		Rst = 2,
	};

	static constexpr uint16_t kRamSelectBit = 0x8000;  // A15: SRAM at $8000-$FFFF, EEPROM below
	static constexpr uint16_t kUartPortMask = 0x00F8;  // A3-A7 decoded
	static constexpr uint16_t kUartPortBase = 0x0080;  // ports $80-$87

	void OnBus() {
		// Active-low strobes read as inactive while the CPU floats them (reset, bus grant).
		const bool memCycle = Pins.NotMREQ.IsLow();
		const bool ioCycle = Pins.NotIORQ.IsLow() && Pins.NotM1.IsHigh();
		const bool read = Pins.NotRD.IsLow();
		const bool write = Pins.NotWR.IsLow();
		const auto addr = Pins.A.Read<uint16_t>();

		const bool ramSel = memCycle && (addr & kRamSelectBit);
		const bool romSel = memCycle && !(addr & kRamSelectBit);
		const bool uartSel = ioCycle && (addr & kUartPortMask) == kUartPortBase;

		Pins.RomCEB.Write(Sim::LogicState::ActiveLow(romSel));
		Pins.RomOEB.Write(Sim::LogicState::ActiveLow(romSel && read));

		Pins.RamCEB.Write(Sim::LogicState::ActiveLow(ramSel));
		Pins.RamOEB.Write(Sim::LogicState::ActiveLow(ramSel && read));
		Pins.RamWEB.Write(Sim::LogicState::ActiveLow(ramSel && write));

		Pins.UartCS2B.Write(Sim::LogicState::ActiveLow(uartSel));
		Pins.UartIORB.Write(Sim::LogicState::ActiveLow(uartSel && read));
		Pins.UartIOWB.Write(Sim::LogicState::ActiveLow(uartSel && write));
	}

	// The UART's INT is active high, the Z80's /INT active low.
	void OnInt() {
		Pins.NotINT.Write(Sim::LogicState::ActiveLow(Pins.UartINT.IsHigh()));
	}

	// The UART's reset is active high, the system reset active low.
	void OnRst() {
		Pins.UartRST.Write(Sim::LogicState::ActiveHigh(Pins.RSTB.IsLow()));
	}
};
