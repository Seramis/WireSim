#pragma once
#include "../../Devices/Cpu/W65C02/W65C02.hpp"
#include "../../Devices/Graphics/Hd44780/Hd44780.hpp"
#include "../../Devices/Io/W65C22/W65C22.hpp"
#include "../../Devices/Memory/Rom/AT28C256/AT28C256.hpp"
#include "../../Devices/Memory/Sram/ParallelSram/ParallelSram.hpp"
#include "../../Devices/Serial/W65C51N/W65C51N.hpp"
#include "../../Simulator/Platform.hpp"
#include "Chipset.hpp"
#include "SerialConsole.hpp"
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <optional>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Ben Eater's breadboard 6502 computer running Microsoft BASIC: a W65C02, 32 KiB
// of SRAM, a 32 KiB EEPROM, a W65C22 VIA driving a 16x2 LCD and a piezo, a
// W65C51N ACIA bridged to a TCP console, and the address-decode glue
// ("Chipset"). See specs/specification.md.
class BenEater6502 {
	Sim::Platform& _p;

	// Common Pins/Buses
	Sim::Pin _clkRaw;
	Sim::Pin _clk;
	Sim::Pin _notRst;
	Sim::Bus _a;
	Sim::Bus _d;
	Sim::Pin _piezo; // VIA PB7 (Timer 1 square wave for BEEP)

  public:
	static constexpr int kConsolePort = 20022;

	// Chips -- public so a MachineHarness shim (BenEater6502.machine-harness.cpp)
	// or other external code can address them directly
	W65C02 _cpu;
	ParallelSram _sram;
	BenEaterChipset _chipset;
	W65C22 _via;
	W65C51N _acia;
	Hd44780 _lcd;

	// The EEPROM's image is a constructor argument, so the chip is built (and
	// wired) by LoadRom() once the image is known -- still before Platform::Start().
	std::optional<AT28C256> _rom;

  private:
	SerialConsole _console;

	// Reset logic
	int _resetCycles = 10;
	termios _originalTermios{};
	bool _termiosSaved = false;

  public:
	BenEater6502(Sim::Platform& p) :
		_p(p),

		_clkRaw(Sim::Pin::New(&_p, "ClkRaw")), _clk(Sim::Pin::New(&_p, "Clk")),
		_notRst(Sim::Pin::New(&_p, "MasterRSTB")), _a(Sim::Bus::New(&_p, 16, "Address")),
		_d(Sim::Bus::New(&_p, 8, "Data")), _piezo(Sim::Pin::New(&_p, "Piezo")),

		_cpu(&_p), _sram(&_p, 15, 8), _chipset(&_p), _via(&_p, W65C22::Variant::N), _acia(&_p),
		_lcd(&_p, Hd44780Variant::Lcd1602, Hd44780ColorScheme::GreenDark, "LCD 16x2"),
		_console(&_p, "Console", kConsolePort, 2048, '\r') {
		Setup();
		Terminal::NullModemConnect(&_console, &_acia);
	}

	// Reads `<romDir>/eater.bin` (as produced by scripts/build-firmware.sh) into
	// the EEPROM and wires it into the machine. Must run before Platform::Start().
	void LoadRom(const std::string& romDir) {
		const std::string path = romDir + "/eater.bin";
		std::ifstream file(path, std::ios::binary);
		if (!file) throw std::runtime_error("Failed to open ROM image: " + path);
		std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (image.size() != AT28C256::kSize) {
			throw std::runtime_error(
				std::format("ROM image {} is {} bytes, expected {}", path, image.size(), AT28C256::kSize)
			);
		}
		if (_rom) throw std::logic_error("ROM already loaded");

		_rom.emplace(&_p, std::move(image));
		SetupRom();
		std::println("Loaded {} ROM bytes", AT28C256::kSize);
	}

	// Advances the simulation by exactly one half clock cycle: ticks
	// diagnostics, toggles the raw clock, holds/releases reset per
	// HoldReset()'s countdown. Returns true the instant a full CPU cycle has
	// just completed (clock was high before this toggle), so a caller can count
	// whole CPU cycles without knowing this machine's clock scheme.
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
	// TickHalfCycle() calls -- non-blocking, takes effect over the following ticks.
	void HoldReset(int halfCycles) {
		_resetCycles = halfCycles;
	}

	void Start(std::optional<uint64_t> cycleLimit = std::nullopt) {
		_console.Start();

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

				// Checked at a clean clock-low boundary, before advancing further.
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

		std::cout << "Running. Console on TCP port " << kConsolePort << ". [s] Status, [r] Reset, [v] Verbose, [d] LCD, [q] Quit"
				  << std::endl
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
			} else if (input == 'v') {
				_p.Enqueue([this]() {
					Sim::Diagnostics& d = _p.GetDiagnostics();
					d.debugMode = !d.debugMode;
					std::println("Verbose diagnostics {}", d.debugMode ? "on" : "off");
				});
			} else if (input == 'd') {
				_lcd.OpenWindow();
			} else if (input == 'q') break;
			else if (input == EOF) break;
		}

		_p.Stop();
		_console.Stop();
		SetTerminalRaw(false);
		if (sim_thread.joinable()) sim_thread.join();
	}

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
		SetupSram();
		SetupVia();
		SetupLcd();
		SetupAcia();
	}

	void SetupCpu() {
		_p.Link(_cpu.CLK, _clkRaw);
		_p.Link(_cpu.CLKout, _clk);
		_p.Link(_cpu.RESB, _notRst);
		_p.Link(_cpu.A, _a);
		_p.Link(_cpu.D, _d);

		_p.Link(_cpu.NMIB, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.IRQB, Sim::Pin::PullUp(&_p)); // open-drain node shared with the ACIA and VIA
		_p.Link(_cpu.RWB, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.RDY, Sim::Pin::PullUp(&_p));
	}

	void SetupChipset() {
		_p.Link(_chipset.Pins.A, _a);
		_p.Link(_chipset.Pins.CLK, _clk);
		_p.Link(_chipset.Pins.CpuRWB, _cpu.RWB);
	}

	void SetupSram() {
		_p.Link(_sram.Pins.A, _a.Slice(0, 15));
		_p.Link(_sram.Pins.D, _d);
		_p.Link(_sram.Pins.NotCE, _chipset.Pins.SramCEB);
		_p.Link(_sram.Pins.NotWE, _chipset.Pins.SramWEB);
		_p.Link(_sram.Pins.NotOE, _chipset.Pins.SramOEB);
	}

	void SetupRom() {
		_p.Link(_rom->Pins.A, _a.Slice(0, 15));
		_p.Link(_rom->Pins.D, _d);
		_p.Link(_rom->Pins.NotCE, _chipset.Pins.RomCEB);
		_p.Link(_rom->Pins.NotOE, _chipset.Pins.RomOEB);
		_p.Link(_rom->Pins.NotWE, Sim::Pin::PullUp(&_p)); // /WE strapped high: the EEPROM is never written
	}

	void SetupVia() {
		_p.Link(_via.Pins.D, _d);
		_p.Link(_via.Pins.RS, _a.Slice(0, 4));
		_p.Link(_via.Pins.RWB, _cpu.RWB);
		_p.Link(_via.Pins.PHI2, _clk);
		_p.Link(_via.Pins.RESB, _notRst);
		_p.Link(_via.Pins.IRQB, _cpu.IRQB);
		_p.Link(_via.Pins.CS1, _chipset.Pins.ViaCS1);
		_p.Link(_via.Pins.CS2B, Sim::Pin::PullDown(&_p));

		// Ports idle low when not driven.
		_p.Link(_via.Pins.PA, Sim::Bus::From(&_p, 8, [&](int) {
					return Sim::Pin::PullDown(&_p);
				}));
		_p.Link(_via.Pins.PB, Sim::Bus::From(&_p, 8, [&](int) {
					return Sim::Pin::PullDown(&_p);
				}));

		_p.Link(_via.Pins.PA[0], _console.Rts);
		_p.Link(_via.Pins.PB[7], _piezo);
	}

	// PB0-PB3 -> DB4-DB7, PB4 -> RS, PB5 -> RW, PB6 -> E; DB0-DB3 unused in 4-bit mode.
	void SetupLcd() {
		_p.Link(_lcd.Pins.DB.Slice(4, 8), _via.Pins.PB.Slice(0, 4));
		_p.Link(_lcd.Pins.RS, _via.Pins.PB[4]);
		_p.Link(_lcd.Pins.RW, _via.Pins.PB[5]);
		_p.Link(_lcd.Pins.E, _via.Pins.PB[6]);
		_p.Link(_lcd.Pins.BL, Sim::Pin::PullUp(&_p));
	}

	void SetupAcia() {
		_p.Link(_acia.Pins.D, _d);
		_p.Link(_acia.Pins.RS0, _a[0]);
		_p.Link(_acia.Pins.RS1, _a[1]);
		_p.Link(_acia.Pins.RWB, _cpu.RWB);
		_p.Link(_acia.Pins.PHI2, _clk);
		_p.Link(_acia.Pins.RESB, _notRst);
		_p.Link(_acia.Pins.IRQB, _cpu.IRQB);
		_p.Link(_acia.Pins.CS0, _chipset.Pins.AciaCS0);
		_p.Link(_acia.Pins.CS1B, Sim::Pin::PullDown(&_p));

		// CTS, DSR and DCD are grounded on the board.
		_p.Link(_acia.Pins.CTSB, Sim::Pin::PullDown(&_p));
		_p.Link(_acia.Pins.DSRB, Sim::Pin::PullDown(&_p));
		_p.Link(_acia.Pins.DCDB, Sim::Pin::PullDown(&_p));
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
	}
};
