# SdlUiTestHooks

Shared SDL plumbing. Let `DeviceHarness` drive and capture device SDL window, through `click` and `screenshot` script verbs (see `DeviceHarness.md`). Live in `src/Support/`, not `src/Devices/` or `src/Testing/` — same reason as [SdlEventHub](SdlEventHub.md): cross-cutting infrastructure, not device. Devices no depend on `src/Testing/`; dependency go other way.

Windowed device inherit `SdlUiTestHooks` **directly**, alongside `Sim::Device`:

```cpp
class MyChip final : public Sim::Device, public SdlUiTestHooks {
  public:
	void OpenWindow() override; // the one method the class must still write
	// CaptureScreenshot()/Click(): inherited, fully implemented
	...
};
```

`CaptureScreenshot` and `Click` fully implemented in `SdlUiTestHooks` — logic same for every device. `OpenWindow` the one pure virtual, touch device own window and open-request state (`_windowOpen`, `_openMutex`, `_openCv`). One base class, not interface plus separate implementation, mean no device write `CaptureScreenshot`/`Click` forwarder. `h.Ui = dev.get();` in `.harness.cpp` shim (`DeviceHandle::Ui` is `SdlUiTestHooks*`) just ordinary public-inheritance upcast.

## Call sites a device must add

Device render loop (thread own `SDL_Window` and `SDL_Renderer`) call three inherited methods, at points it already got:

1. Right after make window and get `windowID`: `WindowOpened(windowID)`.
2. Right after `SDL_RenderPresent(renderer)`, every frame: `AfterPresent(renderer, width, height)`.
3. Right before kill window: `WindowClosed()`.

See `Hd44780::RenderLoop` for pattern.

## Why `Click` needs none of them

Click no touch renderer, no run on render thread. `Click` make real `SDL_MOUSEBUTTONDOWN`/`UP` pair with `SDL_PushEvent`, send to device `windowID` (learn from `WindowOpened`). `SdlEventHub::PollEventsFor(windowID)` already route event from process-global queue by window ID, so fake event arrive same path as real click, device mouse handling need no change. `Click` then wait short (about 40 ms, couple ~16 ms render loop) so next script line see result.

## Why `MinFramesAfterOpen`

`CaptureScreenshot` refuse read frame till few real `SDL_RenderPresent` call happen since window open. Under real compositor (e.g. Wayland), first several present can succeed (`SDL_RenderReadPixels` return success, no `SDL_GetError()`) before compositor attach buffer, readback go solid black. Under `SDL_VIDEODRIVER=dummy`, what `DeviceHarness` default to (see `HarnessMain.cpp`), no compositor, no race, so grace period cost nothing there. Keep capture correct when someone override driver to watch script live.

## Recording, not regression

`CaptureScreenshot` got no `expect-screenshot <golden>` pixel-diff counterpart. Rendering vary across GPU driver, compositor, font rasterizer, reason unrelated to device, so pixel check go flaky. `click` plus ordinary pin-level `expect` the deterministic path, cause UI-driven state (e.g. switch byte) readable through device real pin. `screenshot` for human or CI artifact step to look at, not for pass count.

## Thread safety

One `std::mutex` / `std::condition_variable` pair guard `_windowId`, `_framesSinceOpen`, and single in-flight screenshot request (`_requestPending`, `_requestPath`, `_requestDone`, `_requestResult`). Only one request track at a time, cause `DeviceHarness` script single-device, run one command at a time. `Click` read `_windowId` under lock; `SDL_PushEvent` call outside it, cause SDL event queue already thread-safe for push and poll.