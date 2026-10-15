#pragma once
#include <SDL2/SDL.h>
#include <cstdint>
#include <vector>

// Shared SDL event routing for devices that each run their own SDL window
// on their own render thread (Mrdi1, LedSwitchBox). See SdlEventHub.md for
// why this exists and why it's safe against the Wayland deadlock that
// ruled out a dedicated shared event-pump thread.
namespace SdlEventHub {
// Drains the real, process-global SDL event queue (safe to call
// concurrently from multiple render threads -- internally serialized)
// and returns every pending event addressed to `windowID`. Events
// addressed to a different window are held in that window's own inbox
// for it to collect on its next call, rather than being dropped.
// Events with no window association (e.g. SDL_QUIT) go to every
// caller.
std::vector<SDL_Event> PollEventsFor(Uint32 windowID);

// Drops windowID's inbox. Call right after destroying the window so a
// stale windowID doesn't accumulate undelivered events across the
// device's next open/close cycle.
void Forget(Uint32 windowID);
} // namespace SdlEventHub
