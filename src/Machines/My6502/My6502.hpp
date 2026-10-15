#pragma once
#include "../../Devices/Cpu/W65C02/W65C02.hpp"
#include "../../Devices/Debug/DebugWriter/DebugWriter.hpp"
#include "../../Devices/Graphics/Mrdi1/Mrdi1.hpp"
#include "../../Devices/Memory/Sram/ParallelSram/ParallelSram.hpp"
#include "../../Devices/Panel/LedSwitchBox/LedSwitchBox.hpp"
#include "../../Devices/Register/Register8/Register8.hpp"
#include "../../Devices/Serial/ST16C550/ST16C550.hpp"
#include "../../Devices/Serial/TcpTerminal.hpp"
#include "../../Simulator/Platform.hpp"
#include "Chipset.hpp"
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <termios.h>
#include <thread>
#include <unistd.h>

// Ben Eater's breadboard 6502 computer: a 65C02, 64 KiB of SRAM, a 16C550 UART
// and the address-decode glue logic ("Chipset") wiring them together.
class My6502 {
	Sim::Platform& _p;

	// Common Pins/Buses
	Sim::Pin _clkRaw;
	Sim::Pin _clk;
	Sim::Pin _notRst;
	Sim::Bus _a;
	Sim::Bus _d;

  public:
	// Chips -- public so a MachineHarness shim (My6502.machine-harness.cpp) or
	// other external code can address them directly
	W65C02 _cpu;
	ParallelSram _ram;
	Chipset _chipset;
	ST16C550 _uart;
	Register8 _reg8;
	Mrdi1 _mrdi1;
	LedSwitchBox _ledSwitchBox;
	DebugWriter _dbgWriter;

  private:
	// Terminals
	TcpTerminal _tcpUartTerminal;

	// Reset logic
	int _resetCycles = 10;
	uint64_t _loadedBytes = 0;
	termios _originalTermios{};
	bool _termiosSaved = false;

  public:
	My6502(Sim::Platform& p) :
		_p(p),

		_clkRaw(Sim::Pin::New(&_p, "ClkRaw")), _clk(Sim::Pin::New(&_p, "Clk")),
		_notRst(Sim::Pin::New(&_p, "MasterRSTB")), _a(Sim::Bus::New(&_p, 16, "Address")),
		_d(Sim::Bus::New(&_p, 8, "Data")),

		_cpu(&_p), _ram(&_p, 16, 8), _chipset(&_p), _uart(&_p), _reg8(&_p, "Reg8"), _mrdi1(&_p, "MRDI-1 Display"),
		_ledSwitchBox(&_p, "LedSwitchBox"), _dbgWriter(&_p, "DebugWriter", "debug_writer.log"),
		_tcpUartTerminal("UART", 20021) {
		Setup();
		Terminal::NullModemConnect(&_tcpUartTerminal, &_uart);
	};

	// Loads the four bank images from `romDir` (as produced by
	// scripts/build-firmware.sh) and validates the debug-mode marker.
	// Must run before Start().
	void LoadRom(const std::string& romDir) {
		LoadRomBank(0x0000, romDir + "/main.bank0.bin");
		LoadRomBank(0x4000, romDir + "/main.bank1.bin");
		LoadRomBank(0x8000, romDir + "/main.bank2.bin");
		LoadRomBank(0xC000, romDir + "/main.bank3.bin");

		if (_ram[0xBFFD] != 0x4D || _ram[0xBFFE] != 0x4F || _ram[0xBFFF] > 1) {
			throw std::runtime_error("ROM debug-mode marker missing; rebuild the assembly images");
		}
		Sim::Diagnostics& diagnostics = _p.GetDiagnostics();
		diagnostics.debugMode = _ram[0xBFFF] == 1;
		std::println("Loaded {} ROM bytes; mode={}", _loadedBytes, diagnostics.debugMode ? "debug" : "quiet");
	}

	// Advances the simulation by exactly one half clock cycle: ticks
	// diagnostics, toggles the raw clock, holds/releases reset per
	// HoldReset()'s countdown. Returns true the instant a full CPU cycle
	// has just completed (clock was high before this toggle -- the same
	// value Diagnostics::tick's isCpuCycle argument already keys off), so a
	// caller can count whole CPU cycles without reaching into this
	// machine's own clock scheme. Factored out of Start()'s own
	// per-half-cycle lambda below so MachineHarness can drive it from
	// inside its own single Platform::Start() call instead (see
	// src/Testing/MachineHarness.md).
	bool TickHalfCycle() {
		Sim::Diagnostics& diagnostics = _p.GetDiagnostics();
		const bool clkVal = _clkRaw.Read();
		diagnostics.tick(clkVal);

		_clkRaw.Write(!clkVal);

		if (_resetCycles > 0) {
			_notRst.Write(Sim::LogicState::L);
			--_resetCycles;
		} else {
			_notRst.Write(Sim::LogicState::H);
		}

		return clkVal;
	}

	// Begins holding reset asserted for the given number of upcoming
	// TickHalfCycle() calls -- non-blocking, takes effect over the
	// following ticks. Same effect the interactive 'r' key already has.
	void HoldReset(int halfCycles) {
		_resetCycles = halfCycles;
	}

	void Start(std::optional<uint64_t> cycleLimit = std::nullopt) {
		// Connect Terminal
		_tcpUartTerminal.Start();

		Sim::Diagnostics& diagnostics = _p.GetDiagnostics();
		std::promise<void> simStarted;
		auto simStartedFuture = simStarted.get_future();
		std::thread sim_thread([&]() {
			bool hasStarted = false;
			_p.Start([&]() {
				if (!hasStarted) {
					simStarted.set_value();
					hasStarted = true;
				}

				// Checked at a clean clock-low boundary (both halves of
				// the previous cycle already ran via TickHalfCycle),
				// before advancing any further -- mirrors the original
				// inline check this replaces.
				if (!_clkRaw.Read() && cycleLimit && diagnostics.cpuCycles >= *cycleLimit) {
					std::println("Cycle limit reached: {} CPU cycles", diagnostics.cpuCycles);
					_cpu.DumpState();
					_p.Stop();
					return;
				}

				TickHalfCycle();
			});
		});
		simStartedFuture.wait();

		std::cout << "Running. [s] Status, [r] Reset, [g] Graphics, [l] LED/Switchbox, [q] Quit" << std::endl
				  << std::flush;
		SetTerminalRaw(true);

		pollfd inputFd{STDIN_FILENO, POLLIN, 0};
		while (_p.IsRunning()) {
			int ready = poll(&inputFd, 1, 100);
			if (ready <= 0) continue;

			char input = getchar();
			if (input == 's') {
				_p.Enqueue([this]() {
					std::println(
						"Status: CPU cycles={} half-cycles={} half-cycle rate={}",
						_p.GetDiagnostics().cpuCycles,
						_p.GetDiagnostics().halfCycles,
						_p.GetDiagnostics().GetFormattedFreq()
					);
					_cpu.DumpState();
				});
			} else if (input == 'r') {
				HoldReset(10);
			} else if (input == 'g') {
				_mrdi1.OpenWindow();
			} else if (input == 'l') {
				_ledSwitchBox.OpenWindow();
			} else if (input == 'q') break;
			else if (input == EOF) break;
		}

		_p.Stop();
		_tcpUartTerminal.Stop();
		SetTerminalRaw(false);
		if (sim_thread.joinable()) sim_thread.join();
	};

  private:
	void Setup() {
		// Pull-downs for floating buses
		_p.Link(_notRst, Sim::Pin::PullDown(&_p));
		_p.Link(_clkRaw, Sim::Pin::PullDown(&_p));
		_p.Link(_clk, Sim::Pin::PullDown(&_p));
		_p.Link(_a, Sim::Bus::From(&_p, 16, [&](int) {
					return Sim::Pin::PullDown(&_p);
				}));
		_p.Link(_d, Sim::Bus::From(&_p, 8, [&](int) {
					return Sim::Pin::PullDown(&_p);
				}));

		SetupCpu();
		SetupChipset();
		SetupRam();
		SetupUart();
		SetupReg8();
		SetupMrdi1();
		SetupLedSwitchBox();
		SetupDebugWriter();
	}

	void SetupCpu() {
		_p.Link(_cpu.CLK, _clkRaw);
		_p.Link(_cpu.CLKout, _clk);
		_p.Link(_cpu.RESB, _notRst);
		_p.Link(_cpu.A, _a);
		_p.Link(_cpu.D, _d);

		_p.Link(_cpu.NMIB, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.IRQB, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.RWB, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.RDY, Sim::Pin::PullUp(&_p));
		//_p.Link(_cpu.BE, Sim::Pin::PullUp(&_p));
	}

	void SetupChipset() {
		_p.Link(_chipset.Pins.A, _a);
		_p.Link(_chipset.Pins.CLK, _clk);
		_p.Link(_chipset.Pins.RSTB, _notRst);
		_p.Link(_chipset.Pins.CpuRWB, _cpu.RWB);
		_p.Link(_chipset.Pins.IRQB, _cpu.IRQB);
	}

	void SetupRam() {
		_p.Link(_ram.Pins.A, _a);
		_p.Link(_ram.Pins.D, _d);
		_p.Link(_ram.Pins.NotCE, _chipset.Pins.MemCEB);
		_p.Link(_ram.Pins.NotWE, _chipset.Pins.MemWEB);
		_p.Link(_ram.Pins.NotOE, _chipset.Pins.MemOEB);
	}

	void SetupUart() {
		// Slice address bus for UART (A0-A2)
		_p.Link(_uart.Pins.A, _a.Slice(0, 3));
		_p.Link(_uart.Pins.D, _d);
		_p.Link(_uart.Pins.NotIOR, _chipset.Pins.UartIORB);
		_p.Link(_uart.Pins.IOR, _chipset.Pins.UartIOR);
		_p.Link(_uart.Pins.NotIOW, _chipset.Pins.UartIOWB);
		_p.Link(_uart.Pins.IOW, _chipset.Pins.UartIOW);
		_p.Link(_uart.Pins.CS0, _chipset.Pins.UartCS0);
		_p.Link(_uart.Pins.CS1, _chipset.Pins.UartCS1);
		_p.Link(_uart.Pins.NotCS2, _chipset.Pins.UartCS2B);
		_p.Link(_uart.Pins.INT, _chipset.Pins.UartINT);
		_p.Link(_uart.Pins.INT, Sim::Pin::PullDown(&_p));
		_p.Link(_uart.Pins.RESET, _chipset.Pins.UartRST);
	}

	void SetupReg8() {
		_p.Link(_reg8.Pins.D, _d);
		_p.Link(_reg8.Pins.NotCE, _chipset.Pins.Reg8CEB);
		_p.Link(_reg8.Pins.NotWE, _chipset.Pins.Reg8WEB);
		_p.Link(_reg8.Pins.NotOE, _chipset.Pins.Reg8OEB);
	}

	void SetupMrdi1() {
		_p.Link(_mrdi1.Pins.D, _d);
		_p.Link(_mrdi1.Pins.A0, _a[0]);
		_p.Link(_mrdi1.Pins.NotCE, _chipset.Pins.Mrdi1CEB);
		_p.Link(_mrdi1.Pins.NotWE, _chipset.Pins.Mrdi1WEB);
	}

	void SetupLedSwitchBox() {
		_p.Link(_ledSwitchBox.Pins.D, _d);
		_p.Link(_ledSwitchBox.Pins.NotCE, _chipset.Pins.LedSwitchBoxCEB);
		_p.Link(_ledSwitchBox.Pins.NotWE, _chipset.Pins.LedSwitchBoxWEB);
		_p.Link(_ledSwitchBox.Pins.NotOE, _chipset.Pins.LedSwitchBoxOEB);
	}

	void SetupDebugWriter() {
		_p.Link(_dbgWriter.Pins.D, _d);
		_p.Link(_dbgWriter.Pins.NotCE, _chipset.Pins.DbgWriterCEB);
		_p.Link(_dbgWriter.Pins.NotWE, _chipset.Pins.DbgWriterWEB);
	}

	void LoadRomBank(uint16_t offset, const std::string& path) {
		std::ifstream file(path, std::ios::binary);
		if (!file) throw std::runtime_error("Failed to open ROM image: " + path);

		char byte;
		uint32_t count = 0;
		while (file.get(byte)) {
			_ram[offset + count] = (uint8_t)byte;
			count++;
		}
		_loadedBytes += count;
	}

	void SetTerminalRaw(bool enable) {
		if (enable) {
			if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &_originalTermios) != 0) return;
			termios newt = _originalTermios;
			newt.c_lflag &= ~(ICANON | ECHO);
			if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == 0) _termiosSaved = true;
		} else if (_termiosSaved) {
			tcsetattr(STDIN_FILENO, TCSANOW, &_originalTermios);
			_termiosSaved = false;
		}
	};
};
