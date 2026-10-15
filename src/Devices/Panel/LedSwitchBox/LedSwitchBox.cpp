#include "LedSwitchBox.hpp"
#include "../../../Support/SdlEventHub.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <print>

namespace {
std::mutex g_sdlMutex;
int g_sdlRefCount = 0;

void SdlAcquire() {
	std::lock_guard lock(g_sdlMutex);
	if (g_sdlRefCount++ == 0) {
		SDL_InitSubSystem(SDL_INIT_VIDEO);
	}
}

void SdlRelease() {
	std::lock_guard lock(g_sdlMutex);
	if (--g_sdlRefCount == 0) {
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
	}
}

// Layout constants for the 8 LEDs / 8 switches + their hex readouts.
constexpr int kMarginX = 20;
constexpr int kCellSize = 32;
constexpr int kGap = 8;
constexpr int kStride = kCellSize + kGap;

constexpr int kLedY = 20;
constexpr int kLedHexY = 70;
constexpr int kSwitchY = 150;
constexpr int kSwitchHexY = 215;

constexpr int kSwitchHeight = 48;
constexpr int kHexDigitW = 40;
constexpr int kHexDigitH = 60;
constexpr int kHexGap = 20;

constexpr int kWindowWidth = kMarginX * 2 + 8 * kCellSize + 7 * kGap; // 352
constexpr int kWindowHeight = kSwitchHexY + kHexDigitH + 15;		  // 290

int CellX(int index) {
	return kMarginX + index * kStride;
}
int HexBlockX() {
	return kWindowWidth / 2 - (kHexDigitW * 2 + kHexGap) / 2;
}

// Classic 7-segment encoding (bit0=a..bit6=g) for hex digits 0-F.
constexpr uint8_t kSegments[16] = {
	0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

void DrawDigit(SDL_Renderer* renderer, int x, int y, int w, int h, uint8_t value, SDL_Color color) {
	const uint8_t segs = kSegments[value & 0xF];
	const int t = std::max(3, w / 6);
	const int midY = h / 2;

	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
	auto fill = [&](int rx, int ry, int rw, int rh) {
		SDL_Rect r{x + rx, y + ry, rw, rh};
		SDL_RenderFillRect(renderer, &r);
	};

	if (segs & 0x01) fill(t, 0, w - 2 * t, t);						 // a: top
	if (segs & 0x02) fill(w - t, t, t, midY - t - t / 2);			 // b: top-right
	if (segs & 0x04) fill(w - t, midY + t / 2, t, midY - t - t / 2); // c: bottom-right
	if (segs & 0x08) fill(t, h - t, w - 2 * t, t);					 // d: bottom
	if (segs & 0x10) fill(0, midY + t / 2, t, midY - t - t / 2);	 // e: bottom-left
	if (segs & 0x20) fill(0, t, t, midY - t - t / 2);				 // f: top-left
	if (segs & 0x40) fill(t, midY - t / 2, w - 2 * t, t);			 // g: middle
}

void DrawHexByte(SDL_Renderer* renderer, int x, int y, uint8_t value, SDL_Color color) {
	DrawDigit(renderer, x, y, kHexDigitW, kHexDigitH, value >> 4, color);
	DrawDigit(renderer, x + kHexDigitW + kHexGap, y, kHexDigitW, kHexDigitH, value & 0xF, color);
}
} // namespace

LedSwitchBox::LedSwitchBox(Sim::Platform* p, std::string name) :
	Pins{
		Sim::Bus::New(p, 8, name + ".D"),
		Sim::Pin::New(p, name + ".NotCE"),
		Sim::Pin::New(p, name + ".NotWE"),
		Sim::Pin::New(p, name + ".NotOE")
	},
	_diagnostics(p->GetDiagnostics()), _name(std::move(name)) {
	Pins.NotCE.AddTrigger(this, CTRL::NotCE);
	Pins.NotOE.AddTrigger(this, CTRL::NotOE);
	Pins.NotWE.AddTrigger(this, CTRL::NotWE, false);

	_renderThread = std::thread(&LedSwitchBox::RenderLoop, this);
}

LedSwitchBox::~LedSwitchBox() {
	_stopRequested = true;
	{
		std::lock_guard lock(_openMutex);
		_openRequested = true; // wake RenderLoop if it's waiting to open
	}
	_openCv.notify_all();
	if (_renderThread.joinable()) _renderThread.join();
}

void LedSwitchBox::OpenWindow() {
	if (_windowOpen.load()) return;
	{
		std::lock_guard lock(_openMutex);
		_openRequested = true;
	}
	_openCv.notify_all();
}

void LedSwitchBox::Logic(Sim::TriggerMask mask) {
	const auto isEnabled = Pins.NotCE.IsLow();
	if (!isEnabled) {
		Pins.D.Clear();
		return;
	}

	const auto isWrite = Pins.NotWE.IsLow();
	const auto isRead = !isWrite && Pins.NotOE.IsLow();

	if (isWrite) {
		const auto value = Pins.D.Read<uint8_t>();
		_leds.store(value, std::memory_order_relaxed);
		if (mask.IsSet(CTRL::NotWE)) {
			_diagnostics.Println("{} LEDs <- {:02x}", _name, value);
		}
	}

	if (isRead) {
		Pins.D.Write(_switches.load(std::memory_order_relaxed));
	} else {
		Pins.D.Clear(); // High-Z
	}
}

void LedSwitchBox::RenderLoop() {
	SdlAcquire();

	// Idle-until-opened outer loop: no window at construction, (re)create
	// one each time OpenWindow() is called, run it until closed.
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
			kWindowWidth,
			kWindowHeight,
			SDL_WINDOW_RESIZABLE
		);
		SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;

		if (!window || !renderer) {
			std::println("{}: failed to create SDL window ({})", _name, SDL_GetError());
			if (renderer) SDL_DestroyRenderer(renderer);
			if (window) SDL_DestroyWindow(window);
			continue;
		}

		SDL_RenderSetLogicalSize(renderer, kWindowWidth, kWindowHeight);

		bool closed = false;
		_windowOpen = true;
		const Uint32 windowID = SDL_GetWindowID(window);

		while (!_stopRequested && !closed) {
			for (const SDL_Event& event : SdlEventHub::PollEventsFor(windowID)) {
				if (event.type == SDL_QUIT) closed = true;
				if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
					event.window.windowID == windowID)
					closed = true;

				if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
					const int mx = event.button.x, my = event.button.y;
					if (my >= kSwitchY && my < kSwitchY + kSwitchHeight) {
						for (int i = 0; i < 8; ++i) {
							const int x = CellX(i);
							if (mx >= x && mx < x + kCellSize) {
								_switches.fetch_xor(uint8_t(1u << (7 - i)), std::memory_order_relaxed);
								break;
							}
						}
					}
				}
			}

			const uint8_t leds = _leds.load(std::memory_order_relaxed);
			const uint8_t switches = _switches.load(std::memory_order_relaxed);

			SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
			SDL_RenderClear(renderer);

			// LEDs: lit (bit=1) bright red, unlit dim red. Bit 7 (MSB) on
			// the left, matching the hex digit below it reading MSB-first.
			for (int i = 0; i < 8; ++i) {
				const bool on = (leds >> (7 - i)) & 1;
				SDL_SetRenderDrawColor(renderer, on ? 240 : 70, on ? 40 : 15, on ? 40 : 15, 255);
				SDL_Rect r{CellX(i), kLedY, kCellSize, kCellSize};
				SDL_RenderFillRect(renderer, &r);
			}
			DrawHexByte(renderer, HexBlockX(), kLedHexY, leds, SDL_Color{80, 220, 80, 255});

			// Switches: a slot with a knob at the top (1) or bottom (0).
			for (int i = 0; i < 8; ++i) {
				const bool on = (switches >> (7 - i)) & 1;
				const int x = CellX(i);
				SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
				SDL_Rect slot{x, kSwitchY, kCellSize, kSwitchHeight};
				SDL_RenderFillRect(renderer, &slot);
				SDL_SetRenderDrawColor(renderer, 90, 160, 240, 255);
				SDL_Rect knob{x, on ? kSwitchY : kSwitchY + kSwitchHeight / 2, kCellSize, kSwitchHeight / 2};
				SDL_RenderFillRect(renderer, &knob);
			}
			DrawHexByte(renderer, HexBlockX(), kSwitchHexY, switches, SDL_Color{90, 160, 240, 255});

			SDL_RenderPresent(renderer);
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		_windowOpen = false;
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SdlEventHub::Forget(windowID);
	}

	SdlRelease();
}
