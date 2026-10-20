#pragma once
#include "../../Devices/Cpu/Z80/Z80.hpp"
#include "../../Devices/Io/Z80CTC/Z80CTC.hpp"
#include "../../Devices/Io/Z80SIO/Z80SIO.hpp"
#include "../../Devices/Memory/Rom/SST39SF010/SST39SF010.hpp"
#include "../../Devices/Memory/Storage/SdCard/SdCard.hpp"
#include "../../Devices/Memory/Sram/ParallelSram/ParallelSram.hpp"
#include "../../Devices/Serial/TcpTerminal.hpp"
#include "../../Simulator/Platform.hpp"
#include "Chipset.hpp"
#include <cstdint>
#include <format>
#include <filesystem>
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
#include <utility>
#include <vector>

// A TCP console as the board's RS232 connector sees it: the host does no hardware flow control,
// so it is always clear to send, whatever the firmware does with the SIO's RTS output. Enter
// reaches the CPU as CR, the line end the board's firmware (and CP/M) expects.
class HostConsole final : public TcpTerminal {
  public:
	HostConsole(std::string name, int port) : TcpTerminal(std::move(name), port, 2048, '\r') {
		TcpTerminal::CTS(true);
	}

  protected:
	void CTS(bool) override {}
};

// John Winans' Z80-Retro (2063-Z80) single board computer: a Z80, 128 KiB
// SST39SF010 flash, 512 KiB AS6C4008 SRAM in sixteen 32 KiB banks, a Z80 CTC,
// a Z80 SIO whose two channels are bridged to TCP consoles, an SD card socket
// (SPI bit-banged through the GP ports) and the glue logic ("Chipset") that
// decodes ports, shadows the flash over reset and selects the RAM bank. See
// AGENTS.md.
class Z80Retro {
	Sim::Platform& _p;

	// Common Pins/Buses
	Sim::Pin _clk;
	Sim::Pin _notRst;
	Sim::Pin _notInt; // shared, open-drain /INT line
	Sim::Bus _a;
	Sim::Bus _d;
	Sim::Bus _gpIn; // GP input buffer's inputs

  public:
	static constexpr int kConPort = 20024; // SIO channel A, the RS232 CON connector
	static constexpr int kAuxPort = 20025; // SIO channel B, the RS232 AUX connector

	// Chips -- public so a MachineHarness shim (Z80Retro.machine-harness.cpp)
	// or other external code can address them directly
	Z80 _cpu;
	ParallelSram _ram; // AS6C4008: 512K x 8
	Z80RetroChipset _chipset;
	Z80CTC _ctc;
	Z80SIO _sio;

	SST39SF010 _flash; // SST39SF010: 128K x 8, only the lower 64 KiB reachable

	// The SD card in the socket, if any; see InsertSdCard().
	std::optional<SdCard> _sd;

  private:
	HostConsole _con;
	HostConsole _aux;

	// Reset logic
	int _resetCycles = 10;
	termios _originalTermios{};
	bool _termiosSaved = false;

  public:
	// `user1Jumper`: J9 fitted, which pulls the GP input buffer's USER1 line (bit 5) low.
	Z80Retro(Sim::Platform& p, bool user1Jumper = false) :
		_p(p),

		_clk(Sim::Pin::New(&_p, "Clk")), _notRst(Sim::Pin::New(&_p, "MasterRSTB")),
		_notInt(Sim::Pin::New(&_p, "NotINT")), _a(Sim::Bus::New(&_p, 16, "Address")), _d(Sim::Bus::New(&_p, 8, "Data")),
		_gpIn(Sim::Bus::New(&_p, 8, "GpIn")),

		_cpu(&_p), _ram(&_p, 19, 8), _chipset(&_p), _ctc(&_p), _sio(&_p), _flash(&_p, {}), _con("CON", kConPort), _aux("AUX", kAuxPort) {
		Setup(user1Jumper);
		Terminal::NullModemConnect(&_con, &_sio.HostTerminal(0));
		Terminal::NullModemConnect(&_aux, &_sio.HostTerminal(1));
	}

	static constexpr const char* kSdImageName = "sdcard.img";

	// Puts a card backed by the file `path` (its size is the card's capacity, a multiple of 512
	// bytes) into the socket. The card is read and written in place. Must run before
	// Platform::Start(), at most once.
	void InsertSdCard(const std::string& path) {
		if (_sd) throw std::logic_error("SD card already inserted");
		_sd.emplace(&_p, path, std::filesystem::file_size(path));
		SetupSdCard();
	}

	// Inserts `<romDir>/sdcard.img` if the directory has one and no card is in the socket yet;
	// without a card the MISO line floats high. Returns whether a card is in the socket.
	bool InsertSdCardFrom(const std::string& romDir) {
		const std::string path = romDir + "/" + kSdImageName;
		if (!_sd && std::filesystem::exists(path)) {
			InsertSdCard(path);
			std::println("Inserted SD card {}", path);
		}
		return _sd.has_value();
	}

	// Reads `<romDir>/z80retro.bin` (as produced by scripts/build-firmware.sh)
	// into the flash, and puts `<romDir>/sdcard.img` in the SD socket when it
	// exists. Must run before Platform::Start().
	void LoadRom(const std::string& romDir) {
		const std::string path = romDir + "/z80retro.bin";
		std::ifstream file(path, std::ios::binary);
		if (!file) throw std::runtime_error("Failed to open ROM image: " + path);
		std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (image.empty() || image.size() > SST39SF010::kSize) {
			throw std::runtime_error(
				std::format("ROM image {} is {} bytes, expected 1..{}", path, image.size(), SST39SF010::kSize)
			);
		}

		for (size_t i = 0; i < image.size(); ++i) _flash.Poke(static_cast<uint32_t>(i), image[i]);
		std::println("Loaded {} ROM bytes", image.size());
		InsertSdCardFrom(romDir);
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
		for (const auto& [name, value] : _cpu.DumpRegisters())
			line += std::format(" {}={:X}", name, value);
		std::println("{}", line);
	}

	void Start(std::optional<uint64_t> cycleLimit = std::nullopt) {
		_con.Start();
		_aux.Start();

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

		std::cout << "Running. CON on TCP port " << kConPort << ", AUX on " << kAuxPort
				  << ". [s] Status, [r] Reset, [v] Verbose, [q] Quit" << std::endl
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
		_con.Stop();
		_aux.Stop();
		SetTerminalRaw(false);
		if (sim_thread.joinable()) sim_thread.join();
	}

  private:
	void Setup(bool user1Jumper) {
		// Every address and data line has a pull-up resistor on the board.
		_p.Link(_notRst, Sim::Pin::PullDown(&_p));
		_p.Link(_clk, Sim::Pin::PullDown(&_p));
		_p.Link(_a, Sim::Bus::From(&_p, 16, [&](int) {
					return Sim::Pin::PullUp(&_p);
				}));
		_p.Link(_d, Sim::Bus::From(&_p, 8, [&](int) {
					return Sim::Pin::PullUp(&_p);
				}));

		// GP input: printer status, user jumper (J9 pulls USER1 low), SD card detect and MISO, all
		// pulled up (MISO is driven by the card, when one is inserted).
		_p.Link(_gpIn, Sim::Bus::From(&_p, 8, [&](int bit) {
					return bit == kUser1Bit && user1Jumper ? Sim::Pin::PullDown(&_p) : Sim::Pin::PullUp(&_p);
				}));

		SetupCpu();
		SetupChipset();
		SetupRam();
		SetupFlash();
		SetupCtc();
		SetupSio();
	}

	void SetupCpu() {
		_p.Link(_cpu.Pins.CLK, _clk);
		_p.Link(_cpu.Pins.NotRESET, _notRst);
		_p.Link(_cpu.Pins.A, _a);
		_p.Link(_cpu.Pins.D, _d);

		// Pull-ups on the request and strobe lines.
		_p.Link(_cpu.Pins.NotWAIT, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotNMI, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotBUSREQ, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotMREQ, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotIORQ, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotRD, Sim::Pin::PullUp(&_p));
		_p.Link(_cpu.Pins.NotWR, Sim::Pin::PullUp(&_p));

		// CTC and SIO share the open-drain /INT line with the CPU.
		_p.Link(_cpu.Pins.NotINT, _notInt);
		_p.Link(_notInt, Sim::Pin::PullUp(&_p));
	}

	void SetupChipset() {
		_p.Link(_chipset.Pins.A, _a);
		_p.Link(_chipset.Pins.D, _d);
		_p.Link(_chipset.Pins.NotMREQ, _cpu.Pins.NotMREQ);
		_p.Link(_chipset.Pins.NotIORQ, _cpu.Pins.NotIORQ);
		_p.Link(_chipset.Pins.NotRD, _cpu.Pins.NotRD);
		_p.Link(_chipset.Pins.NotM1, _cpu.Pins.NotM1);
		_p.Link(_chipset.Pins.NotRESET, _notRst);
		_p.Link(_chipset.Pins.GpIn, _gpIn);
	}

	void SetupRam() {
		// A0-A14 from the CPU, A15-A18 from the bank select gates.
		for (int i = 0; i < 15; ++i)
			_p.Link(_ram.Pins.A[i], _a[i]);
		for (int i = 0; i < 4; ++i)
			_p.Link(_ram.Pins.A[15 + i], _chipset.Pins.SramBank[i]);
		_p.Link(_ram.Pins.D, _d);
		_p.Link(_ram.Pins.NotCE, _cpu.Pins.NotMREQ);
		_p.Link(_ram.Pins.NotWE, _cpu.Pins.NotWR);
		_p.Link(_ram.Pins.NotOE, _chipset.Pins.SramNotOE);
	}

	void SetupFlash() {
		// A0-A15 from the CPU; A16 (the flash bank jumper J12) is pulled low.
		for (int i = 0; i < 16; ++i)
			_p.Link(_flash.Pins.A[i], _a[i]);
		_p.Link(_flash.Pins.A[16], Sim::Pin::PullDown(&_p));
		_p.Link(_flash.Pins.D, _d);
		_p.Link(_flash.Pins.NotCE, _chipset.Pins.FlashNotCE);
		_p.Link(_flash.Pins.NotOE, _cpu.Pins.NotRD);
		_p.Link(_flash.Pins.NotWE, _cpu.Pins.NotWR);
	}

	void SetupSdCard() {
		// GP output bits 0-2 are MOSI, CLK and /SS; the card answers on GP input bit 7.
		_p.Link(_sd->Pins.MOSI, _chipset.Pins.GpOut[kSdMosiBit]);
		_p.Link(_sd->Pins.SCLK, _chipset.Pins.GpOut[kSdClkBit]);
		_p.Link(_sd->Pins.NotCS, _chipset.Pins.GpOut[kSdNotCsBit]);
		_p.Link(_sd->Pins.MISO, _gpIn[kSdMisoBit]);
	}

	void SetupCtc() {
		_p.Link(_ctc.Pins.D, _d);
		_p.Link(_ctc.Pins.CS0, _a[0]);
		_p.Link(_ctc.Pins.CS1, _a[1]);
		_p.Link(_ctc.Pins.NotCE, _chipset.Pins.CtcNotCE);
		_p.Link(_ctc.Pins.CLK, _clk);
		_p.Link(_ctc.Pins.NotM1, _cpu.Pins.NotM1);
		_p.Link(_ctc.Pins.NotIORQ, _cpu.Pins.NotIORQ);
		_p.Link(_ctc.Pins.NotRD, _cpu.Pins.NotRD);
		_p.Link(_ctc.Pins.NotRESET, _notRst);
		_p.Link(_ctc.Pins.NotINT, _notInt);

		// First in the interrupt daisy chain, ahead of the SIO.
		_p.Link(_ctc.Pins.IEI, Sim::Pin::PullUp(&_p));

		// Channels 0 and 3 count the system clock; channels 1 and 2 take the 1.8432 MHz baud clock,
		// which is not modeled.
		_p.Link(_ctc.Pins.ClkTrg[0], _clk);
		_p.Link(_ctc.Pins.ClkTrg[3], _clk);
		_p.Link(_ctc.Pins.ClkTrg[1], Sim::Pin::PullDown(&_p));
		_p.Link(_ctc.Pins.ClkTrg[2], Sim::Pin::PullDown(&_p));
	}

	void SetupSio() {
		_p.Link(_sio.Pins.D, _d);
		_p.Link(_sio.Pins.BNotA, _a[0]);
		_p.Link(_sio.Pins.CNotD, _a[1]);
		_p.Link(_sio.Pins.NotCE, _chipset.Pins.SioNotCE);
		_p.Link(_sio.Pins.CLK, _clk);
		_p.Link(_sio.Pins.NotM1, _cpu.Pins.NotM1);
		_p.Link(_sio.Pins.NotIORQ, _cpu.Pins.NotIORQ);
		_p.Link(_sio.Pins.NotRD, _cpu.Pins.NotRD);
		_p.Link(_sio.Pins.NotRESET, _notRst);
		_p.Link(_sio.Pins.NotINT, _notInt);

		// Second in the daisy chain, behind the CTC; its own IEO is unconnected.
		_p.Link(_sio.Pins.IEI, _ctc.Pins.IEO);

		// Both channels: CTS and DCD tied low, SYNC and RxD unconnected (parked inactive). The
		// channels are byte-linked to the TCP consoles, so the serial clocks are unused.
		for (auto* channel : {&_sio.Pins.A, &_sio.Pins.B}) {
			_p.Link(channel->NotCTS, Sim::Pin::PullDown(&_p));
			_p.Link(channel->NotDCD, Sim::Pin::PullDown(&_p));
			_p.Link(channel->NotSYNC, Sim::Pin::PullUp(&_p));
			_p.Link(channel->RxD, Sim::Pin::PullUp(&_p));
		}
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

	static constexpr int kUser1Bit = 5;	  // GP input bit wired to the J9 USER1 jumper
	static constexpr int kSdMisoBit = 7;  // GP input bit wired to the card's MISO
	static constexpr int kSdMosiBit = 0;  // GP output bits wired to the card
	static constexpr int kSdClkBit = 1;
	static constexpr int kSdNotCsBit = 2;
};
