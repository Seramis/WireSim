#pragma once
#include "../../Simulator/Platform.hpp"

class Chipset final : public Sim::Device {
  public:
	struct ChipsetPins {
		Sim::Bus A, D;
		Sim::Pin CLK, RSTB, IRQB;

		// CPU
		Sim::Pin CpuRWB;

		// Mem
		Sim::Pin MemCEB, MemWEB, MemOEB;

		// UART
		Sim::Pin UartIORB, UartIOR, UartIOWB, UartIOW, UartCS0, UartCS1, UartCS2B, UartINT, UartRST;

		// Register8 (Verilog)
		Sim::Pin Reg8CEB, Reg8WEB, Reg8OEB;

		// MRDI-1 display
		Sim::Pin Mrdi1CEB, Mrdi1WEB;

		// LedSwitchBox
		Sim::Pin LedSwitchBoxCEB, LedSwitchBoxWEB, LedSwitchBoxOEB;

		// DebugWriter
		Sim::Pin DbgWriterCEB, DbgWriterWEB;

		ChipsetPins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 16, "A")), D(Sim::Bus::New(p, 8, "D")), CLK(Sim::Pin::New(p, "CLK")),
			RSTB(Sim::Pin::New(p, "RSTB")), IRQB(Sim::Pin::New(p, "IRQB")),
			// CPU
			CpuRWB(Sim::Pin::New(p, "Cpu.RWB")),
			// Mem
			MemCEB(Sim::Pin::New(p, "Mem.CEB")), MemWEB(Sim::Pin::New(p, "Mem.MemWEB")),
			MemOEB(Sim::Pin::New(p, "Mem.MemOEB")),
			// UART
			UartIORB(Sim::Pin::New(p, "Uart.IORB")), UartIOR(Sim::Pin::New(p, "Uart.UartIOR")),
			UartIOWB(Sim::Pin::New(p, "Uart.UartIOWB")), UartIOW(Sim::Pin::New(p, "Uart.UartIOW")),
			UartCS0(Sim::Pin::New(p, "Uart.UartCS0")), UartCS1(Sim::Pin::New(p, "Uart.UartCS1")),
			UartCS2B(Sim::Pin::New(p, "Uart.UartCS2B")), UartINT(Sim::Pin::New(p, "Uart.UartINT")),
			UartRST(Sim::Pin::New(p, "Uart.UartRST")),
			// Register8 (Verilog)
			Reg8CEB(Sim::Pin::New(p, "Reg8.CEB")), Reg8WEB(Sim::Pin::New(p, "Reg8.WEB")),
			Reg8OEB(Sim::Pin::New(p, "Reg8.OEB")),
			// MRDI-1 display
			Mrdi1CEB(Sim::Pin::New(p, "Mrdi1.CEB")), Mrdi1WEB(Sim::Pin::New(p, "Mrdi1.WEB")),
			// LedSwitchBox
			LedSwitchBoxCEB(Sim::Pin::New(p, "LedSwitchBox.CEB")),
			LedSwitchBoxWEB(Sim::Pin::New(p, "LedSwitchBox.WEB")),
			LedSwitchBoxOEB(Sim::Pin::New(p, "LedSwitchBox.OEB")),
			// DebugWriter
			DbgWriterCEB(Sim::Pin::New(p, "DbgWriter.CEB")), DbgWriterWEB(Sim::Pin::New(p, "DbgWriter.WEB")) {}
	};

	ChipsetPins Pins;

	Chipset(Sim::Platform* p) : Pins(p) {
		Pins.CLK.AddTrigger(this);
		Pins.CpuRWB.AddTrigger(this);
		Pins.A.AddTrigger(this);

		Pins.RSTB.AddTrigger(this, LogicFlags::RST);
		Pins.UartINT.AddTrigger(this, LogicFlags::INT);
	}

	void Logic(const Sim::TriggerMask mask) {
		if (mask.IsSet(LogicFlags::RST)) {
			OnRst();
		}
		if (mask.IsSet(LogicFlags::INT)) {
			OnInt();
		}
		if (mask.IsSet(LogicFlags::IO)) {
			OnIo();
		}
	}

  private:
	enum class LogicFlags : uint8_t {
		IO = 0,
		INT = 1,
		RST = 2,
	};

	// Which device the address bus currently selects. OnIo() fires on every
	// CLK/RWB/address change, but on this memory map the overwhelming
	// majority of bus cycles target Memory -- so rather than redriving every
	// peripheral's CE/OE/WE on every call (relying on Pin::Write's own
	// unchanged-value check to absorb the cost), it tracks the selected
	// target and only touches the pins for whichever one is currently
	// selected, falling back to a full redrive on the rare cycle where the
	// selection itself changes.
	enum class Target : uint8_t {
		Memory,
		Uart,
		Reg8,
		DbgWriter,
		Mrdi1,
		LedSwitchBox
	};

	const uint16_t _uartAddressRange[2] = {0xC000, 0xC007};
	const uint16_t _reg8Address = 0xC008;
	const uint16_t _dbgWriterAddress = 0xC009;
	const uint16_t _mrdi1AddressRange[2] = {0xC00A, 0xC00B}; // even base: A0=0 is IR, A0=1 is DR
	const uint16_t _ledSwitchBoxAddress = 0xC00C;

	Target _lastTarget = Target::Memory;
	bool _targetKnown = false;

	Target DecodeTarget(uint16_t addr) const {
		if (_uartAddressRange[0] <= addr && addr <= _uartAddressRange[1]) return Target::Uart;
		if (addr == _reg8Address) return Target::Reg8;
		if (addr == _dbgWriterAddress) return Target::DbgWriter;
		if (_mrdi1AddressRange[0] <= addr && addr <= _mrdi1AddressRange[1]) return Target::Mrdi1;
		if (addr == _ledSwitchBoxAddress) return Target::LedSwitchBox;
		return Target::Memory;
	}

	// Deasserts every peripheral's CE/OE/WE (and the UART's CS/IOR/IOW
	// lines) -- the "nothing selected" state each one sits in except while
	// it's the current target. Only runs on an actual target change (see
	// OnIo), not on every call.
	void DeselectAll() {
		Pins.MemCEB.Write(Sim::LogicState::H);
		Pins.MemOEB.Write(Sim::LogicState::H);
		Pins.MemWEB.Write(Sim::LogicState::H);

		Pins.Reg8CEB.Write(Sim::LogicState::H);
		Pins.Reg8OEB.Write(Sim::LogicState::H);
		Pins.Reg8WEB.Write(Sim::LogicState::H);

		Pins.Mrdi1CEB.Write(Sim::LogicState::H);
		Pins.Mrdi1WEB.Write(Sim::LogicState::H);

		Pins.LedSwitchBoxCEB.Write(Sim::LogicState::H);
		Pins.LedSwitchBoxOEB.Write(Sim::LogicState::H);
		Pins.LedSwitchBoxWEB.Write(Sim::LogicState::H);

		Pins.DbgWriterCEB.Write(Sim::LogicState::H);
		Pins.DbgWriterWEB.Write(Sim::LogicState::H);

		Pins.UartCS0.Write(Sim::LogicState::L);
		Pins.UartCS1.Write(Sim::LogicState::L);
		Pins.UartCS2B.Write(Sim::LogicState::H);
		Pins.UartIORB.Write(Sim::LogicState::H);
		Pins.UartIOR.Write(Sim::LogicState::L);
		Pins.UartIOWB.Write(Sim::LogicState::H);
		Pins.UartIOW.Write(Sim::LogicState::L);
	}

	void OnIo() {
		const auto addr = Pins.A.Read<uint16_t>();
		const auto periphActive = Pins.CLK.IsHigh();
		const auto isRead = Pins.CpuRWB.IsHigh();

		const Target target = DecodeTarget(addr);
		if (!_targetKnown || target != _lastTarget) {
			DeselectAll();
			_lastTarget = target;
			_targetKnown = true;
		}

		const auto readStrobe = Sim::LogicState::ActiveLow(isRead && periphActive);
		const auto writeStrobe = Sim::LogicState::ActiveLow(!isRead && periphActive);

		switch (target) {
			case Target::Memory:
				Pins.MemCEB.Write(Sim::LogicState::L);
				Pins.MemOEB.Write(readStrobe);
				Pins.MemWEB.Write(writeStrobe);
				break;
			case Target::Reg8:
				Pins.Reg8CEB.Write(Sim::LogicState::L);
				Pins.Reg8OEB.Write(readStrobe);
				Pins.Reg8WEB.Write(writeStrobe);
				break;
			case Target::DbgWriter:
				Pins.DbgWriterCEB.Write(Sim::LogicState::L);
				Pins.DbgWriterWEB.Write(writeStrobe);
				break;
			case Target::Mrdi1:
				Pins.Mrdi1CEB.Write(Sim::LogicState::L);
				Pins.Mrdi1WEB.Write(writeStrobe);
				break;
			case Target::LedSwitchBox:
				Pins.LedSwitchBoxCEB.Write(Sim::LogicState::L);
				Pins.LedSwitchBoxOEB.Write(readStrobe);
				Pins.LedSwitchBoxWEB.Write(writeStrobe);
				break;
			case Target::Uart:
				Pins.UartCS0.Write(Sim::LogicState::H);
				Pins.UartCS1.Write(Sim::LogicState::H);
				Pins.UartCS2B.Write(Sim::LogicState::L);
				Pins.UartIORB.Write(Sim::LogicState::ActiveLow(isRead && periphActive));
				Pins.UartIOR.Write(Sim::LogicState::ActiveHigh(isRead && periphActive));
				Pins.UartIOWB.Write(Sim::LogicState::ActiveLow(!isRead && periphActive));
				Pins.UartIOW.Write(Sim::LogicState::ActiveHigh(!isRead && periphActive));
				break;
		}
	}

	void OnInt() {
		Pins.IRQB.Write(!Pins.UartINT.Read().TriHigh());
	}

	void OnRst() {
		Pins.UartRST.Write(Pins.RSTB.Read().TriLow());
	}
};
