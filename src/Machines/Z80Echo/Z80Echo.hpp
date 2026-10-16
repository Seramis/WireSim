#pragma once
#include "../../Devices/Cpu/Z80/Z80.hpp"
#include "../../Devices/Memory/Rom/AT28C256/AT28C256.hpp"
#include "../../Devices/Memory/Sram/ParallelSram/ParallelSram.hpp"
#include "../../Devices/Serial/ST16C550/ST16C550.hpp"
#include "../../Devices/Serial/TcpTerminal.hpp"
#include "../../Simulator/Platform.hpp"
#include "Chipset.hpp"
#include <cstdint>
#include <format>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <optional>
#include <poll.h>
#include <print>
#include <stdexcept>
#include <string>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <vector>

// A Z80 computer that echoes back everything received on its UART: a Z80, a
// 32 KiB AT28C256 EEPROM at $0000-$7FFF, a 32 KiB AS6C62256 SRAM at $8000-$FFFF,
// an ST16C550 UART at I/O ports $80-$87 bridged to a TCP console, and the
// address-decode glue ("Chipset"). See AGENTS.md.
class Z80Echo {
	Sim::Platform& _p;

	// Common Pins/Buses
	Sim::Pin _clk;
	Sim::Pin _notRst;
	Sim::Bus _a;
	Sim::Bus _d;

  public:
	static constexpr int kConsolePort = 20023;

	// Chips -- public so a MachineHarness shim (Z80Echo.machine-harness.cpp)
	// or other external code can address them directly
	Z80 _cpu;
	ParallelSram _ram; // AS6C62256: 32K x 8
	Z80EchoChipset _chipset;
	ST16C550 _uart;

	// The EEPROM's image is a constructor argument, so the chip is built (and
	// wired) by LoadRom() once the image is known -- still before Platform::Start().
	std::optional<AT28C256> _rom;

  private:
	TcpTerminal _console;

	// Reset logic
	int _resetCycles = 10;
	termios _originalTermios{};
	bool _termiosSaved = false;

  public:
	Z80Echo(Sim::Platform& p) :
		_p(p),

		_clk(Sim::Pin::New(&_p, "Clk")), _notRst(Sim::Pin::New(&_p, "MasterRSTB")),
		_a(Sim::Bus::New(&_p, 16, "Address")), _d(Sim::Bus::New(&_p, 8, "Data")),

		_cpu(&_p), _ram(&_p, 15, 8), _chipset(&_p), _uart(&_p), _console("UART", kConsolePort) {
		Setup();
		Terminal::NullModemConnect(&_console, &_uart);
	}

	// Reads `<romDir>/z80echo.bin` (as produced by scripts/build-firmware.sh)
	// into the EEPROM and wires it into the machine. Must run before Platform::Start().
	void LoadRom(const std::string& romDir) {
		const std::string path = romDir + "/z80echo.bin";
		std::ifstream file(path, std::ios::binary);
		if (!file) throw std::runtime_error("Failed to open ROM image: " + path);
		std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (image.empty() || image.size() > AT28C256::kSize) {
			throw std::runtime_error(
				std::format("ROM image {} is {} bytes, expected 1..{}", path, image.size(), AT28C256::kSize)
			);
		}
		if (_rom) throw std::logic_error("ROM already loaded");

		const auto loadedBytes = image.size();
		_rom.emplace(&_p, std::move(image));
		SetupRom();
		std::println("Loaded {} ROM bytes", loadedBytes);
	}

	// Advances the simulation by exactly one half clock cycle: ticks
	// diagnostics, toggles the clock, holds/releases reset per HoldReset()'s
	// countdown. Returns true the instant a full clock cycle (one T-state) has
	// just completed (clock was high before this toggle), so a caller can count
	// whole cycles without knowing this machine's clock scheme.
	bool TickHalfCycle() {
		Sim::Diagnostics& diagnostics = _p.GetDiagnostics();
		const bool clkVal = _clk.Read();
		diagnostics.tick(clkVal);

		_clk.Write(!clkVal);

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

	void DumpState() const {
		std::string line = "Z80 state";
		for (const auto& [name, value] : _cpu.DumpRegisters()) line += std::format(" {}={:X}", name, value);
		std::println("{}", line);
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
				if (!_clk.Read() && cycleLimit && diagnostics.cpuCycles >= *cycleLimit) {
					std::println("Cycle limit reached: {} clock cycles", diagnostics.cpuCycles);
					DumpState();
					_p.Stop();
					return;
				}

				TickHalfCycle();
			});
		});
		simStartedFuture.wait();

		std::cout << "Running. Console on TCP port " << kConsolePort << ". [s] Status, [r] Reset, [v] Verbose, [q] Quit"
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
						"Status: clock cycles={} half-cycles={} half-cycle rate={}",
						_p.GetDiagnostics().cpuCycles,
						_p.GetDiagnostics().halfCycles,
						_p.GetDiagnostics().GetFormattedFreq()
					);
					DumpState();
				});
			} else if (input == 'r') {
				HoldReset(10);
			} else if (input == 'v') {
				_p.Enqueue([this]() {
					Sim::Diagnostics& d = _p.GetDiagnostics();
					d.debugMode = !d.debugMode;
					std::println("Verbose diagnostics {}", d.debugMode ? "on" : "off");
				});
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
	}

	void SetupCpu() {
		_p.Link(_cpu.Pins.CLK, _clk);
		_p.Link(_cpu.Pins.NotRESET, _notRst);
		_p.Link(_cpu.Pins.A, _a);
		_p.Link(_cpu.Pins.D, _d);

		// Request lines the board does not use are tied inactive.
		_p.Link(_cpu.Pins.NotWAIT, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotNMI, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotBUSREQ, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotINT, _chipset.Pins.NotINT);
		_p.Link(_cpu.Pins.NotINT, Sim::Pin::PullUp(&_p));
	}

	void SetupChipset() {
		_p.Link(_chipset.Pins.A, _a);
		_p.Link(_chipset.Pins.NotMREQ, _cpu.Pins.NotMREQ);
		_p.Link(_chipset.Pins.NotIORQ, _cpu.Pins.NotIORQ);
		_p.Link(_chipset.Pins.NotRD, _cpu.Pins.NotRD);
		_p.Link(_chipset.Pins.NotWR, _cpu.Pins.NotWR);
		_p.Link(_chipset.Pins.NotM1, _cpu.Pins.NotM1);
		_p.Link(_chipset.Pins.RSTB, _notRst);
	}

	void SetupRam() {
		_p.Link(_ram.Pins.A, _a.Slice(0, 15));
		_p.Link(_ram.Pins.D, _d);
		_p.Link(_ram.Pins.NotCE, _chipset.Pins.RamCEB);
		_p.Link(_ram.Pins.NotWE, _chipset.Pins.RamWEB);
		_p.Link(_ram.Pins.NotOE, _chipset.Pins.RamOEB);
	}

	void SetupRom() {
		_p.Link(_rom->Pins.A, _a.Slice(0, 15));
		_p.Link(_rom->Pins.D, _d);
		_p.Link(_rom->Pins.NotCE, _chipset.Pins.RomCEB);
		_p.Link(_rom->Pins.NotOE, _chipset.Pins.RomOEB);
		_p.Link(_rom->Pins.NotWE, Sim::Pin::PullUp(&_p)); // /WE strapped high: the EEPROM is never written
	}

	void SetupUart() {
		_p.Link(_uart.Pins.A, _a.Slice(0, 3));
		_p.Link(_uart.Pins.D, _d);
		_p.Link(_uart.Pins.NotCS2, _chipset.Pins.UartCS2B);
		_p.Link(_uart.Pins.NotIOR, _chipset.Pins.UartIORB);
		_p.Link(_uart.Pins.NotIOW, _chipset.Pins.UartIOWB);
		_p.Link(_uart.Pins.RESET, _chipset.Pins.UartRST);
		_p.Link(_uart.Pins.INT, _chipset.Pins.UartINT);
		_p.Link(_uart.Pins.INT, Sim::Pin::PullDown(&_p));

		// CS0 and CS1 are tied high, the active-high IOR / IOW strobes tied low.
		_p.Link(_uart.Pins.CS0, Sim::Pin::PullUp(&_p));
		_p.Link(_uart.Pins.CS1, Sim::Pin::PullUp(&_p));
		_p.Link(_uart.Pins.IOR, Sim::Pin::PullDown(&_p));
		_p.Link(_uart.Pins.IOW, Sim::Pin::PullDown(&_p));
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
