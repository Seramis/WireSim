#include "SdlUiTestHooks.hpp"
#include <thread>

void SdlUiTestHooks::WindowOpened(Uint32 windowId) {
	std::lock_guard lock(_mutex);
	_windowId = windowId;
	_framesSinceOpen = 0;
	_cv.notify_all();
}

void SdlUiTestHooks::WindowClosed() {
	std::lock_guard lock(_mutex);
	_windowId = 0;
	_framesSinceOpen = 0;
	if (_requestPending) {
		_requestResult = false;
		_requestDone = true;
		_requestPending = false;
	}
	_cv.notify_all();
}

void SdlUiTestHooks::AfterPresent(SDL_Renderer* renderer, int width, int height) {
	std::lock_guard lock(_mutex);
	if (_framesSinceOpen < MinFramesAfterOpen) ++_framesSinceOpen;
	if (!_requestPending || _framesSinceOpen < MinFramesAfterOpen) return;

	SDL_Surface* surf = SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0);
	bool ok = surf != nullptr;
	if (ok) {
		ok = SDL_RenderReadPixels(renderer, nullptr, surf->format->format, surf->pixels, surf->pitch) == 0;
		if (ok) ok = SDL_SaveBMP(surf, _requestPath.c_str()) == 0;
		SDL_FreeSurface(surf);
	}
	_requestResult = ok;
	_requestDone = true;
	_requestPending = false;
	_cv.notify_all();
}

bool SdlUiTestHooks::CaptureScreenshot(const std::string& path, std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;

	std::unique_lock lock(_mutex);
	if (!_cv.wait_until(lock, deadline, [this] {
			return _windowId != 0;
		}))
		return false;

	_requestPath = path;
	_requestPending = true;
	_requestDone = false;
	if (!_cv.wait_until(lock, deadline, [this] {
			return _requestDone;
		})) {
		_requestPending = false;
		return false;
	}
	return _requestResult;
}

bool SdlUiTestHooks::Click(int x, int y, UiMouseButton button) {
	const auto deadline = std::chrono::steady_clock::now() + DefaultClickTimeout;

	Uint32 windowId;
	{
		std::unique_lock lock(_mutex);
		if (!_cv.wait_until(lock, deadline, [this] {
				return _windowId != 0;
			}))
			return false;
		windowId = _windowId;
	}

	const Uint8 sdlButton = button == UiMouseButton::Right	  ? SDL_BUTTON_RIGHT
							: button == UiMouseButton::Middle ? SDL_BUTTON_MIDDLE
															  : SDL_BUTTON_LEFT;

	SDL_Event down{};
	down.type = SDL_MOUSEBUTTONDOWN;
	down.button.windowID = windowId;
	down.button.button = sdlButton;
	down.button.state = SDL_PRESSED;
	down.button.clicks = 1;
	down.button.x = x;
	down.button.y = y;
	SDL_PushEvent(&down);

	SDL_Event up = down;
	up.type = SDL_MOUSEBUTTONUP;
	up.button.state = SDL_RELEASED;
	SDL_PushEvent(&up);

	// Give the device's own render thread a couple of its ~16ms loop
	// iterations to actually poll and act on the synthetic event, so a
	// script's very next line (often `expect` or `screenshot`) sees the
	// result without needing its own manual delay.
	std::this_thread::sleep_for(std::chrono::milliseconds(40));
	return true;
}
