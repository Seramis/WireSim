#pragma once
#include "../../../Simulator/Platform.hpp"
#include "../../../Support/SdlUiTestHooks.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Physical character count/layout; see Hd44780.md for everything else
// (instruction decode, DDRAM/CGRAM addressing, 4-bit nibble protocol,
// known simplifications).
enum class Hd44780Variant : uint8_t {
	Lcd1602, // 16 cols x 2 rows
	Lcd2004	 // 20 cols x 4 rows
};

// Both real modules are STN "Positive, Y-G" (yellow-green) by datasheet
// default; blue-background/white-text is the other extremely common
// commercial variant. Purely a render-time choice -- doesn't affect Logic().
enum class Hd44780ColorScheme : uint8_t {
	GreenDark,
	BlueWhite
};

// HD44780-compatible character LCD controller (the chip inside both the
// LCD1602 and LCD2004 modules -- same instruction set, same 80-byte DDRAM,
// same CGRAM; only the glass's row/column count and address-to-cell mapping
// differ). See Hd44780.md alongside this file for the full protocol and the
// SDL rendering/threading model (idle until the window is first opened).
//
// Also inherits SdlUiTestHooks (src/Support/SdlUiTestHooks.hpp), so
// DeviceHarness's `screenshot`/`click` verbs work against it with no
// per-device wrapper methods -- CaptureScreenshot()/Click() come fully
// implemented from that base; only OpenWindow() (genuinely device-specific)
// is this class's own. See SdlUiTestHooks.md. This is a second, independent
// vtable on top of Sim::Device's own non-virtual Logic() dispatch --
// cold-path only (window open/screenshot/click), so it doesn't affect that
// hot-path design.
class Hd44780 final : public Sim::Device, public SdlUiTestHooks {
  public:
	struct Hd44780Pins {
		Sim::Bus DB; // 8-bit; only DB4-DB7 carry data once Function Set selects 4-bit mode
		Sim::Pin RS; // 0 = instruction/busy-address register, 1 = data register
		Sim::Pin RW; // 0 = write, 1 = read
		Sim::Pin E;	 // enable strobe -- the only trigger this device registers
		Sim::Pin BL; // backlight enable, active-high (see Hd44780.md -- not a real HD44780 pin)
	};
	Hd44780Pins Pins;

	Hd44780(
		Sim::Platform* p,
		Hd44780Variant variant,
		Hd44780ColorScheme colorScheme = Hd44780ColorScheme::GreenDark,
		std::string name = "HD44780"
	);
	~Hd44780();

	void Logic(Sim::TriggerMask mask);

	// Opens the SDL window if it isn't already open; no-op otherwise. Safe
	// to call from any thread (does not touch Pins/nets). The only method of
	// SdlUiTestHooks this class implements itself -- CaptureScreenshot()/
	// Click() are inherited, fully implemented (see class doc comment above
	// and SdlUiTestHooks.md). Hd44780 has no clickable UI, so an inherited
	// Click() is a no-op as far as this device's own state is concerned,
	// but it's still real, working SDL event injection -- not a
	// stub -- for interface completeness.
	void OpenWindow() override;

  private:
	// Two separate trigger registrations on the same E pin -- see
	// Hd44780.md's "Why E needs both an immediate and a stable trigger":
	// driving read data (and releasing DB) must react within the same
	// settle pass E changes in, but the write-commit/nibble-advance must
	// only run once, on a confirmed, fully-settled falling edge.
	enum class Trig : uint8_t {
		EImmediate = 0,
		ECommit = 1,
		Backlight = 2
	};
	enum class NibblePhase : uint8_t {
		High,
		Low
	};

	static constexpr int LineBytes = 40;  // per-line DDRAM size, real hardware constant
	static constexpr int DdramSize = 128; // 7-bit address counter range
	static constexpr int CgramSize = 64;  // 6-bit address counter range (8 chars x 8 rows)

	Sim::Diagnostics& _diagnostics;
	std::string _name;
	const int _cols;
	const int _rows;
	const Hd44780ColorScheme _colorScheme;

	// --- Model state: only ever touched from the sim thread (Logic()) ---
	std::vector<uint8_t> _ddram;
	std::vector<uint8_t> _cgram;
	uint8_t _ac = 0;
	bool _addressingCgram = false;
	bool _entryIncrement = true;
	bool _entryShift = false;
	bool _displayOn = false;
	bool _cursorOn = false;
	bool _blinkOn = false;
	int _displayShiftOffset = 0;
	bool _fourBitMode = false;
	NibblePhase _nibblePhase = NibblePhase::High;
	uint8_t _pendingHighNibble = 0;

	// E-pulse edge tracking. _lastRs/_lastRw/_lastCaptured are refreshed by
	// the immediate handler every time E is seen high; _pulseActive is
	// touched only by the stable handler, so the two can't clobber each
	// other regardless of which phase runs first within a step (see
	// Hd44780.md).
	bool _pulseActive = false;
	bool _lastRs = false;
	bool _lastRw = false;
	uint8_t _lastCaptured = 0;

	std::atomic<bool> _backlightOn{false};

	// --- Render-facing snapshot, guarded by _stateMutex; written at the
	// end of every completed E pulse, read once per frame by RenderLoop ---
	struct RenderSnapshot {
		std::vector<uint8_t> ddram;
		std::vector<uint8_t> cgram;
		uint8_t ac = 0;
		bool addressingCgram = false;
		bool displayOn = false;
		bool cursorOn = false;
		bool blinkOn = false;
		int displayShiftOffset = 0;
	};
	mutable std::mutex _stateMutex;
	RenderSnapshot _snapshot;
	std::atomic<bool> _dirty{true};

	std::thread _renderThread;
	std::atomic<bool> _stopRequested{false};
	std::atomic<bool> _windowOpen{false};
	bool _openRequested = false;
	std::mutex _openMutex;
	std::condition_variable _openCv;

	void DriveRead(bool rs);
	void CommitPulse(bool rs, bool rw, uint8_t capturedNibbleOrByte);
	void ExecuteCommand(uint8_t byte);
	void WriteDataRegister(uint8_t byte);
	uint8_t ReadDataRegisterValue() const;
	void AdvanceAddressCounter();
	void ShiftDisplay(int delta);
	void PublishSnapshot();

	// (row, visibleCol) -> raw DDRAM address, folding in `shiftOffset` and
	// (for the 4-row variant) the row-pairing quirk -- see Hd44780.md's
	// "DDRAM addressing". Only reads _cols/_rows (fixed at construction),
	// so it's safe to call from the render thread against a snapshot's
	// shift offset rather than the live (sim-thread-only) one.
	uint8_t VisibleToAddr(int row, int col, int shiftOffset) const;
	// Inverse of VisibleToAddr, for locating the cursor; false if `addr`
	// currently falls outside the visible window.
	bool AddrToVisiblePos(uint8_t addr, int shiftOffset, int& row, int& col) const;

	void RenderLoop();
};
