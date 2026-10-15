#include "Hd44780.hpp"
#include "../../../Support/SdlEventHub.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <print>

namespace {
// Own SDL subsystem init/quit refcount, guarded by a mutex since SDL's own
// internal refcounting isn't safe against concurrent Init/Quit from multiple
// threads. This device owns its window/thread independently of any other
// windowed device; the only piece actually shared across them is
// SdlEventHub (src/Support/SdlEventHub.hpp).
std::mutex g_sdlMutex;
int g_sdlRefCount = 0;

void SdlAcquire() {
	std::lock_guard lock(g_sdlMutex);
	if (g_sdlRefCount++ == 0) {
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
		SDL_InitSubSystem(SDL_INIT_VIDEO);
	}
}

void SdlRelease() {
	std::lock_guard lock(g_sdlMutex);
	if (--g_sdlRefCount == 0) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
}

// 5x8 dot-matrix font, bits 4..0 = pixel columns (bit4 = leftmost), one row
// per byte, 8 rows -- the same convention as HD44780 CGRAM data (see
// Hd44780.md's "Character rendering"). Covers printable ASCII 0x20-0x7E.
// Rasterized from hd44780.otf (alongside this file): that font's outlines
// sit exactly on a 5-column x 7-row grid (ascent=2000, descent=0, so no
// glyph goes below the font's own baseline), sampled here at the center of
// each cell. Row 7 (the bottommost dot row) is therefore always blank,
// matching the real CGROM's reserved cursor-underline row; since the font
// has no descender space, g/j/p/q/y are drawn entirely within rows 0-6
// rather than dropping into row 7 the way the real chip's CGROM does.
constexpr uint8_t Font5x8[0x7F - 0x20][8] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x20 ' '
	{0x04, 0x04, 0x04, 0x04, 0x00, 0x00, 0x04, 0x00}, // 0x21 '!'
	{0x0a, 0x0a, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x22 '"'
	{0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a, 0x00}, // 0x23 '#'
	{0x04, 0x0f, 0x14, 0x0e, 0x05, 0x1e, 0x04, 0x00}, // 0x24 '$'
	{0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00}, // 0x25 '%'
	{0x0c, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0d, 0x00}, // 0x26 '&'
	{0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x27 '\''
	{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00}, // 0x28 '('
	{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00}, // 0x29 ')'
	{0x00, 0x04, 0x15, 0x0e, 0x15, 0x04, 0x00, 0x00}, // 0x2a '*'
	{0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00, 0x00}, // 0x2b '+'
	{0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08, 0x00}, // 0x2c ','
	{0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00}, // 0x2d '-'
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c, 0x00}, // 0x2e '.'
	{0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, // 0x2f '/'
	{0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e, 0x00}, // 0x30 '0'
	{0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00}, // 0x31 '1'
	{0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f, 0x00}, // 0x32 '2'
	{0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e, 0x00}, // 0x33 '3'
	{0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02, 0x00}, // 0x34 '4'
	{0x1f, 0x10, 0x1e, 0x01, 0x01, 0x01, 0x1e, 0x00}, // 0x35 '5'
	{0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e, 0x00}, // 0x36 '6'
	{0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00}, // 0x37 '7'
	{0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e, 0x00}, // 0x38 '8'
	{0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c, 0x00}, // 0x39 '9'
	{0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00, 0x00}, // 0x3a ':'
	{0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x04, 0x08, 0x00}, // 0x3b ';'
	{0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00}, // 0x3c '<'
	{0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00, 0x00}, // 0x3d '='
	{0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00}, // 0x3e '>'
	{0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04, 0x00}, // 0x3f '?'
	{0x0e, 0x11, 0x01, 0x0d, 0x15, 0x15, 0x0e, 0x00}, // 0x40 '@'
	{0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x00}, // 0x41 'A'
	{0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, 0x00}, // 0x42 'B'
	{0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, 0x00}, // 0x43 'C'
	{0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c, 0x00}, // 0x44 'D'
	{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f, 0x00}, // 0x45 'E'
	{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10, 0x00}, // 0x46 'F'
	{0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f, 0x00}, // 0x47 'G'
	{0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, 0x00}, // 0x48 'H'
	{0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f, 0x00}, // 0x49 'I'
	{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00}, // 0x4a 'J'
	{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00}, // 0x4b 'K'
	{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x00}, // 0x4c 'L'
	{0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11, 0x00}, // 0x4d 'M'
	{0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x00}, // 0x4e 'N'
	{0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00}, // 0x4f 'O'
	{0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x00}, // 0x50 'P'
	{0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, 0x00}, // 0x51 'Q'
	{0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, 0x00}, // 0x52 'R'
	{0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e, 0x00}, // 0x53 'S'
	{0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, // 0x54 'T'
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00}, // 0x55 'U'
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00}, // 0x56 'V'
	{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a, 0x00}, // 0x57 'W'
	{0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, 0x00}, // 0x58 'X'
	{0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x00}, // 0x59 'Y'
	{0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, 0x00}, // 0x5a 'Z'
	{0x0e, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0e, 0x00}, // 0x5b '['
	{0x11, 0x0a, 0x1f, 0x04, 0x1f, 0x04, 0x04, 0x00}, // 0x5c '\\'
	{0x0e, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0e, 0x00}, // 0x5d ']'
	{0x04, 0x0a, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x5e '^'
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00}, // 0x5f '_'
	{0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0x60 '`'
	{0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f, 0x00}, // 0x61 'a'
	{0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1e, 0x00}, // 0x62 'b'
	{0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e, 0x00}, // 0x63 'c'
	{0x01, 0x01, 0x0d, 0x13, 0x11, 0x11, 0x0f, 0x00}, // 0x64 'd'
	{0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e, 0x00}, // 0x65 'e'
	{0x06, 0x09, 0x08, 0x1c, 0x08, 0x08, 0x08, 0x00}, // 0x66 'f'
	{0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01, 0x0e, 0x00}, // 0x67 'g'
	{0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00}, // 0x68 'h'
	{0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e, 0x00}, // 0x69 'i'
	{0x02, 0x00, 0x02, 0x02, 0x02, 0x12, 0x0c, 0x00}, // 0x6a 'j'
	{0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}, // 0x6b 'k'
	{0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x00}, // 0x6c 'l'
	{0x00, 0x00, 0x1a, 0x15, 0x15, 0x11, 0x11, 0x00}, // 0x6d 'm'
	{0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00}, // 0x6e 'n'
	{0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e, 0x00}, // 0x6f 'o'
	{0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10, 0x00}, // 0x70 'p'
	{0x00, 0x00, 0x0d, 0x13, 0x0f, 0x01, 0x01, 0x00}, // 0x71 'q'
	{0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00}, // 0x72 'r'
	{0x00, 0x00, 0x0e, 0x10, 0x0e, 0x01, 0x1e, 0x00}, // 0x73 's'
	{0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06, 0x00}, // 0x74 't'
	{0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0d, 0x00}, // 0x75 'u'
	{0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04, 0x00}, // 0x76 'v'
	{0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a, 0x00}, // 0x77 'w'
	{0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x00}, // 0x78 'x'
	{0x00, 0x00, 0x11, 0x11, 0x0f, 0x01, 0x0e, 0x00}, // 0x79 'y'
	{0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f, 0x00}, // 0x7a 'z'
	{0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02, 0x00}, // 0x7b '{'
	{0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, // 0x7c '|'
	{0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08, 0x00}, // 0x7d '}'
	{0x00, 0x04, 0x02, 0x1f, 0x02, 0x04, 0x00, 0x00}, // 0x7e '~'
};

// Dot-matrix layout. Each character cell is drawn as a 5x8 grid of square
// dots (lit vs unlit, never a blank background) -- the actual visual
// signature of a real passive-matrix character LCD, where "off" segments
// are still faintly visible against the backlight.
constexpr int DotSize = 4;
constexpr int DotGap = 2;
constexpr int DotPitch = DotSize + DotGap;
constexpr int GlyphW = 5 * DotSize + 4 * DotGap;
constexpr int GlyphH = 8 * DotSize + 7 * DotGap;
constexpr int CellPadX = 7;
constexpr int CellPadY = 7;
constexpr int CellW = GlyphW + 2 * CellPadX;
constexpr int CellH = GlyphH + 2 * CellPadY;
constexpr int Margin = 18;

struct Palette {
	SDL_Color bgOn, textOn, bgOff, textOff;
};

constexpr Palette GreenPalette{
	{132, 178, 65, 255}, // bgOn: classic yellow-green STN backlight, slightly desaturated
	{20, 40, 12, 255},	 // textOn: dark green ink
	{96, 104, 92, 255},	 // bgOff: unlit, desaturated toward gray
	{60, 66, 58, 255}	 // textOff: faint, barely-contrasting ink when unlit
};
constexpr Palette BluePalette{
	{34, 48, 158, 255},	  // bgOn: classic LCD-blue backlight
	{232, 240, 255, 255}, // textOn: near-white ink
	{86, 90, 104, 255},	  // bgOff: unlit, desaturated toward gray
	{64, 68, 78, 255}	  // textOff: faint ink when unlit
};

const Palette& PaletteFor(Hd44780ColorScheme scheme) {
	return scheme == Hd44780ColorScheme::BlueWhite ? BluePalette : GreenPalette;
}

void DrawCell(
	SDL_Renderer* renderer,
	int cellX,
	int cellY,
	const uint8_t* glyphRows /* nullptr = blank */,
	SDL_Color bg,
	SDL_Color on,
	SDL_Color off,
	bool underline,
	bool invert
) {
	SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
	SDL_Rect cellRect{cellX, cellY, CellW, CellH};
	SDL_RenderFillRect(renderer, &cellRect);

	const int glyphX = cellX + CellPadX;
	const int glyphY = cellY + CellPadY;
	for (int row = 0; row < 8; ++row) {
		const uint8_t bits = glyphRows ? glyphRows[row] : 0;
		const bool underlineRow = underline && row == 7;
		for (int col = 0; col < 5; ++col) {
			bool lit = ((bits >> (4 - col)) & 1) != 0;
			if (underlineRow) lit = true;
			if (invert) lit = !lit;
			const SDL_Color& c = lit ? on : off;
			SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
			SDL_Rect dot{glyphX + col * DotPitch, glyphY + row * DotPitch, DotSize, DotSize};
			SDL_RenderFillRect(renderer, &dot);
		}
	}
}
} // namespace

Hd44780::Hd44780(Sim::Platform* p, Hd44780Variant variant, Hd44780ColorScheme colorScheme, std::string name) :
	Pins{
		Sim::Bus::New(p, 8, name + ".DB"),
		Sim::Pin::New(p, name + ".RS"),
		Sim::Pin::New(p, name + ".RW"),
		Sim::Pin::New(p, name + ".E"),
		Sim::Pin::New(p, name + ".BL")
	},
	_diagnostics(p->GetDiagnostics()), _name(std::move(name)), _cols(variant == Hd44780Variant::Lcd1602 ? 16 : 20),
	_rows(variant == Hd44780Variant::Lcd1602 ? 2 : 4), _colorScheme(colorScheme), _ddram(DdramSize, 0x20),
	_cgram(CgramSize, 0) {
	// Matches the HD44780's own documented power-on-reset state (Hd44780.md
	// / "Initializing by Internal Reset Circuit"): display cleared, AC=0,
	// 8-bit mode, display/cursor/blink off, increment-no-shift entry mode.
	// Every member above already defaults to that state; only the
	// snapshot needs seeding.
	Pins.E.AddTrigger(this, Trig::EImmediate, true);
	Pins.E.AddTrigger(this, Trig::ECommit, false);
	Pins.BL.AddTrigger(this, Trig::Backlight);

	_snapshot.ddram = _ddram;
	_snapshot.cgram = _cgram;

	_renderThread = std::thread(&Hd44780::RenderLoop, this);
}

Hd44780::~Hd44780() {
	_stopRequested = true;
	{
		std::lock_guard lock(_openMutex);
		_openRequested = true; // wake RenderLoop if it's waiting to open
	}
	_openCv.notify_all();
	if (_renderThread.joinable()) _renderThread.join();
}

void Hd44780::OpenWindow() {
	if (_windowOpen.load()) return;
	{
		std::lock_guard lock(_openMutex);
		_openRequested = true;
	}
	_openCv.notify_all();
}

void Hd44780::Logic(Sim::TriggerMask mask) {
	if (mask.IsSet(Trig::Backlight)) {
		_backlightOn.store(Pins.BL.IsHigh(), std::memory_order_relaxed);
	}

	// Both branches below only react to E's own triggers, so check for
	// either before reading any pin -- cheaper than even one read, and
	// skips RS/RW/E entirely on a Backlight-only call.
	if (!mask.IsSetAny(Trig::EImmediate, Trig::ECommit)) return;

	const bool eHigh = Pins.E.IsHigh();

	// Immediate: combinational reactions to E's current level, re-run
	// freely as the settle loop iterates. Must be immediate (not stable)
	// so a read's drive is visible within the same step/cycle E rises in
	// -- see Hd44780.md.
	if (mask.IsSet(Trig::EImmediate)) {
		if (eHigh) {
			_lastRs = Pins.RS.IsHigh();
			_lastRw = Pins.RW.IsHigh();
			if (_lastRw) {
				DriveRead(_lastRs);
			} else {
				Pins.DB.Clear(); // MPU drives DB during a write
				_lastCaptured = _fourBitMode ? Pins.DB.Slice(4, 8).Read<uint8_t>() : Pins.DB.Read<uint8_t>();
			}
		} else {
			Pins.DB.Clear(); // High-Z whenever E is low
		}
	}

	// Stable: commit exactly once, on a fully-settled falling edge.
	// _pulseActive is only ever touched here, never by the immediate
	// branch above, so the two can't race regardless of which phase a
	// given step resolves first (see Hd44780.md).
	if (mask.IsSet(Trig::ECommit)) {
		if (eHigh) {
			_pulseActive = true;
		} else if (_pulseActive) {
			_pulseActive = false;
			CommitPulse(_lastRs, _lastRw, _lastCaptured);
		}
	}
}

void Hd44780::DriveRead(bool rs) {
	// Busy flag (DB7) always reads 0: no internal-operation timing is
	// modeled, every instruction completes within this one Logic() call
	// (see Hd44780.md's "Known simplifications").
	const uint8_t full = rs ? ReadDataRegisterValue() : static_cast<uint8_t>(_ac & 0x7F);
	if (!_fourBitMode) {
		Pins.DB.Write(full);
		return;
	}
	const uint8_t nibble = (_nibblePhase == NibblePhase::High) ? (full >> 4) : (full & 0x0F);
	Pins.DB.Slice(4, 8).Write(nibble);
	Pins.DB.Slice(0, 4).Clear(); // DB0-3 unused/unconnected in 4-bit mode
}

void Hd44780::CommitPulse(bool rs, bool rw, uint8_t capturedNibbleOrByte) {
	if (!_fourBitMode) {
		if (!rw) {
			if (rs) WriteDataRegister(capturedNibbleOrByte);
			else ExecuteCommand(capturedNibbleOrByte);
		} else if (rs) {
			AdvanceAddressCounter(); // busy/address read (rs=0) never advances AC
		}
		PublishSnapshot();
		return;
	}

	// 4-bit mode: two E pulses per register access, high nibble first (see
	// Hd44780.md's "4-bit nibble protocol"). _nibblePhase starts High at
	// construction and is restored to High right after every completed
	// access below, so it's always correct regardless of how/when Function
	// Set switched DL -- including the classic single-nibble 8-bit-to-4-bit
	// handshake, which never touches this field at all.
	if (_nibblePhase == NibblePhase::High) {
		if (!rw) _pendingHighNibble = capturedNibbleOrByte & 0x0F;
		_nibblePhase = NibblePhase::Low;
	} else {
		if (!rw) {
			const uint8_t full = static_cast<uint8_t>((_pendingHighNibble << 4) | (capturedNibbleOrByte & 0x0F));
			if (rs) WriteDataRegister(full);
			else ExecuteCommand(full);
		} else if (rs) {
			AdvanceAddressCounter();
		}
		_nibblePhase = NibblePhase::High;
	}
	PublishSnapshot();
}

void Hd44780::ExecuteCommand(uint8_t byte) {
	// Priority-decoded in the same strictly-decreasing-bit order Table 6 of
	// the HD44780U datasheet encodes it in -- each instruction's topmost set
	// bit is unique, so checking DB7 down to DB0 in turn and taking the
	// first match mirrors the chip's own decode.
	if (byte & 0x80) { // Set DDRAM address
		_ac = byte & 0x7F;
		_addressingCgram = false;
	} else if (byte & 0x40) { // Set CGRAM address
		_ac = byte & 0x3F;
		_addressingCgram = true;
	} else if (byte & 0x20) { // Function set: DL only (see Hd44780.md -- N/F are stored nowhere, this device's row/col
							  // count is fixed by Variant)
		_fourBitMode = ((byte >> 4) & 1) == 0;
	} else if (byte & 0x10) { // Cursor or display shift
		const bool shiftDisplay = (byte >> 3) & 1;
		const bool right = (byte >> 2) & 1;
		if (shiftDisplay) {
			ShiftDisplay(right ? -1 : +1);
		} else {
			// Cursor-only move: the datasheet calls this "meaningless" when
			// AC addresses CGRAM, so no special-casing -- just move AC
			// within whichever address space is currently selected.
			const uint8_t width = _addressingCgram ? 0x3F : 0x7F;
			_ac = static_cast<uint8_t>((_ac + (right ? 1 : -1)) & width);
		}
	} else if (byte & 0x08) { // Display on/off control
		_displayOn = (byte >> 2) & 1;
		_cursorOn = (byte >> 1) & 1;
		_blinkOn = byte & 1;
	} else if (byte & 0x04) { // Entry mode set
		_entryIncrement = (byte >> 1) & 1;
		_entryShift = byte & 1;
	} else if (byte & 0x02) { // Return home
		_ac = 0;
		_addressingCgram = false;
		_displayShiftOffset = 0;
	} else if (byte & 0x01) { // Clear display
		std::fill(_ddram.begin(), _ddram.end(), 0x20);
		_ac = 0;
		_addressingCgram = false;
		// Not explicitly stated for this instruction (only Return Home says
		// so); matching it anyway since a cleared screen with a stale
		// scroll offset would otherwise still show blank columns shifted
		// out of position -- see Hd44780.md.
		_displayShiftOffset = 0;
	}
	// byte == 0x00: no instruction defined at this priority level; no-op.

	_diagnostics.Println("{} IR <- {:02x}", _name, byte);
}

void Hd44780::WriteDataRegister(uint8_t byte) {
	if (_addressingCgram) {
		_cgram[_ac & 0x3F] = byte & 0x1F; // only bits 4..0 (5 pixel columns) are meaningful
	} else {
		_ddram[_ac & 0x7F] = byte;
	}
	_diagnostics.Println("{} {} <- {:02x} @ {:02x}", _name, _addressingCgram ? "CGRAM" : "DDRAM", byte, _ac);
	AdvanceAddressCounter();
}

uint8_t Hd44780::ReadDataRegisterValue() const {
	return _addressingCgram ? _cgram[_ac & 0x3F] : _ddram[_ac & 0x7F];
}

void Hd44780::AdvanceAddressCounter() {
	const int delta = _entryIncrement ? 1 : -1;
	if (_addressingCgram) {
		_ac = static_cast<uint8_t>((_ac + delta) & 0x3F);
	} else {
		_ac = static_cast<uint8_t>((_ac + delta) & 0x7F);
		if (_entryShift) ShiftDisplay(delta); // Entry Mode Set's S bit: "accompanies display shift"
	}
}

void Hd44780::ShiftDisplay(int delta) {
	_displayShiftOffset = ((_displayShiftOffset + delta) % LineBytes + LineBytes) % LineBytes;
}

void Hd44780::PublishSnapshot() {
	std::lock_guard lock(_stateMutex);
	_snapshot.ddram = _ddram;
	_snapshot.cgram = _cgram;
	_snapshot.ac = _ac;
	_snapshot.addressingCgram = _addressingCgram;
	_snapshot.displayOn = _displayOn;
	_snapshot.cursorOn = _cursorOn;
	_snapshot.blinkOn = _blinkOn;
	_snapshot.displayShiftOffset = _displayShiftOffset;
	_dirty.store(true, std::memory_order_relaxed);
}

uint8_t Hd44780::VisibleToAddr(int row, int col, int shiftOffset) const {
	const int line = row % 2;
	const int rowBase = (row >= 2) ? _cols : 0; // nonzero only for the 4-row variant
	const int lcol = ((rowBase + col + shiftOffset) % LineBytes + LineBytes) % LineBytes;
	return static_cast<uint8_t>((line << 6) | lcol);
}

bool Hd44780::AddrToVisiblePos(uint8_t addr, int shiftOffset, int& row, int& col) const {
	const int line = (addr >> 6) & 1;
	const int lcol = addr & 0x3F;
	if (lcol >= LineBytes) return false; // outside the real 40-byte line
	const int shifted = ((lcol - shiftOffset) % LineBytes + LineBytes) % LineBytes;
	if (_rows <= 2) {
		if (shifted >= _cols) return false;
		row = line;
		col = shifted;
		return true;
	}
	if (shifted < _cols) {
		row = line;
		col = shifted;
		return true;
	}
	if (shifted < 2 * _cols) {
		row = line + 2;
		col = shifted - _cols;
		return true;
	}
	return false;
}

void Hd44780::RenderLoop() {
	SdlAcquire();

	const int windowWidth = Margin * 2 + _cols * CellW;
	const int windowHeight = Margin * 2 + _rows * CellH;
	const Palette& palette = PaletteFor(_colorScheme);

	while (!_stopRequested) {
		{
			std::unique_lock lock(_openMutex);
			_openCv.wait(lock, [this] {
				return _openRequested || _stopRequested;
			});
			_openRequested = false;
		}
		if (_stopRequested) break;

		SDL_Window* window = SDL_CreateWindow(
			_name.c_str(),
			SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED,
			windowWidth,
			windowHeight,
			SDL_WINDOW_RESIZABLE
		);
		SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;

		if (!window || !renderer) {
			std::println("{}: failed to create SDL window ({})", _name, SDL_GetError());
			if (renderer) SDL_DestroyRenderer(renderer);
			if (window) SDL_DestroyWindow(window);
			continue;
		}

		SDL_RenderSetLogicalSize(renderer, windowWidth, windowHeight);

		bool closed = false;
		_windowOpen = true;
		const Uint32 windowID = SDL_GetWindowID(window);
		_dirty = true; // force a repaint of whatever's already in the model on (re)open
		WindowOpened(windowID);

		RenderSnapshot local;
		while (!_stopRequested && !closed) {
			for (const SDL_Event& event : SdlEventHub::PollEventsFor(windowID)) {
				if (event.type == SDL_QUIT) closed = true;
				if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
					event.window.windowID == windowID)
					closed = true;
			}

			if (_dirty.exchange(false)) {
				std::lock_guard lock(_stateMutex);
				local = _snapshot;
			}

			const bool backlightOn = _backlightOn.load(std::memory_order_relaxed);
			const SDL_Color bg = backlightOn ? palette.bgOn : palette.bgOff;
			const SDL_Color on = backlightOn ? palette.textOn : palette.textOff;
			// "Off" dots: a faint shadow of the lit color against the
			// current background -- real passive-matrix dots are never
			// perfectly invisible even unselected (see Hd44780.md).
			const SDL_Color off{
				static_cast<Uint8>((bg.r * 3 + on.r) / 4),
				static_cast<Uint8>((bg.g * 3 + on.g) / 4),
				static_cast<Uint8>((bg.b * 3 + on.b) / 4),
				255
			};

			const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
								   std::chrono::steady_clock::now().time_since_epoch()
			)
								   .count();
			const bool blinkPhase = (nowMs / 400) % 2 == 0;

			int cursorRow = -1, cursorCol = -1;
			const bool cursorVisible = local.displayOn && (local.cursorOn || local.blinkOn) &&
									   AddrToVisiblePos(local.ac, local.displayShiftOffset, cursorRow, cursorCol);

			SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
			SDL_RenderClear(renderer);

			for (int row = 0; row < _rows; ++row) {
				for (int col = 0; col < _cols; ++col) {
					const uint8_t addr = VisibleToAddr(row, col, local.displayShiftOffset);
					const uint8_t code = local.displayOn ? local.ddram[addr & 0x7F] : 0x20;

					const uint8_t* glyph = nullptr;
					if ((code & 0xF8) == 0x00 || (code & 0xF8) == 0x08) {
						glyph = &local.cgram[(code & 0x07) * 8]; // user-defined CGRAM character
					} else if (code >= 0x20 && code <= 0x7E) {
						glyph = Font5x8[code - 0x20];
					}
					// Anything else (0x10-0x1F, 0x7F-0xFF): not modeled,
					// renders blank -- see Hd44780.md.

					const bool isCursorCell = cursorVisible && row == cursorRow && col == cursorCol;
					const bool underline = isCursorCell && local.cursorOn;
					const bool invert = isCursorCell && local.blinkOn && blinkPhase;

					DrawCell(
						renderer, Margin + col * CellW, Margin + row * CellH, glyph, bg, on, off, underline, invert
					);
				}
			}

			SDL_RenderPresent(renderer);
			AfterPresent(renderer, windowWidth, windowHeight);
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		_windowOpen = false;
		WindowClosed();
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SdlEventHub::Forget(windowID);
	}

	SdlRelease();
}
