#pragma once
#include <SDL2/SDL.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

// Mouse button for SdlUiTestHooks::Click -- its own enum (not a raw
// SDL_BUTTON_* Uint8) so DeviceHarness's script layer doesn't need to
// depend on SDL's button constants directly.
enum class UiMouseButton {
	Left,
	Right,
	Middle
};

// A device that owns an SDL window inherits this directly, alongside
// Sim::Device, to pick up DeviceHarness's `click`/`screenshot` script
// verbs:
//
//   class MyChip final : public Sim::Device, public SdlUiTestHooks {
//     public:
//       void OpenWindow() override; // the one method still device-specific
//       // CaptureScreenshot()/Click(): inherited, fully implemented
//       ...
//   };
//
// `CaptureScreenshot`/`Click` come fully implemented here (their logic is
// identical for every device). `OpenWindow` stays pure virtual -- that one
// really is device-specific, since it touches that device's own
// window/open-request state -- so a device that forgets it simply fails to
// compile, the same as missing `Logic(TriggerMask)`. See SdlUiTestHooks.md
// for the three call sites (WindowOpened/AfterPresent/WindowClosed) a
// device's render loop must add to make this work, and why `Click` needs
// none of them.
class SdlUiTestHooks {
  public:
	virtual ~SdlUiTestHooks() = default;

	// Opens the device's window if not already open; no-op otherwise. Each
	// implementer already has this as a concrete method for its own
	// production use (e.g. a machine's console binding it to a keypress) --
	// this base class just makes it part of the harness-testable surface.
	virtual void OpenWindow() = 0;

	// Blocks (up to `timeout`) until a window is open and has rendered a
	// few real frames, then saves the current frame to `path` as a BMP
	// (SDL_SaveBMP -- no new dependency). Returns false on timeout or save
	// failure. For recording/manual review, not pixel-diff regression
	// testing -- see SdlUiTestHooks.md's "Recording, not regression".
	bool
	CaptureScreenshot(const std::string& path, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

	// Blocks (up to a fixed internal timeout) until a window is open, then
	// synthesizes a real down+up mouse click at (x, y) in the window's
	// logical pixel coordinates, addressed to that window --
	// indistinguishable, to the device's own event handling, from a real
	// click. Returns false on timeout (no window ever opened).
	bool Click(int x, int y, UiMouseButton button = UiMouseButton::Left);

	void WindowOpened(Uint32 windowId);
	void WindowClosed();

	// Call once per render-loop iteration, immediately after
	// SDL_RenderPresent(renderer).
	void AfterPresent(SDL_Renderer* renderer, int width, int height);

  private:
	// Default timeout for Click(), which (unlike CaptureScreenshot) takes
	// no caller-supplied one -- see SdlUiTestHooks.md.
	static constexpr std::chrono::milliseconds DefaultClickTimeout{2000};

	// Frames to let pass after a window opens before trusting a readback --
	// see SdlUiTestHooks.md's "Why MinFramesAfterOpen".
	static constexpr int MinFramesAfterOpen = 3;

	std::mutex _mutex;
	std::condition_variable _cv;
	Uint32 _windowId = 0;
	int _framesSinceOpen = 0;

	bool _requestPending = false;
	std::string _requestPath;
	bool _requestDone = false;
	bool _requestResult = false;
};
