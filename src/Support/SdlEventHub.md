# SdlEventHub

Shared SDL event routing for every device that own SDL window on own render thread. Lives in `src/Support/`, not `src/Devices/` — cross-cutting infrastructure, shared across devices and categories (see `AGENTS.md` repository map).

## Why a shared poller

SDL keep one event queue for whole process, not one per window. `SDL_PollEvent()` pump OS events into that queue *and* pop its head, no matter what window caller own. `windowID` on returned event just informational, not filter.

With one window open, device calling `SDL_PollEvent()` from own render loop harmless — only caller. With two or more windows, each render thread race same queue, whoever call first win event, even if belong to other window. Device that handle only few event types (e.g. `SDL_QUIT`) would silently drop rest, including other window's mouse click.

Also SDL thread-safety requirement: `SDL_PollEvent` call `SDL_PumpEvents`, only one thread may call at a time.

## How it works

`PollEventsFor(windowID)` what every render thread call instead of `SDL_PollEvent()`. Take mutex, drain real SDL queue once (so `SDL_PumpEvents` never enter concurrently), route each event by window ID into per-window inbox (`g_inboxes`). Caller get only own window's events; other window's events wait in its inbox for that window's next call. Events with no window (`SDL_QUIT`) broadcast to every registered inbox.

`Forget(windowID)` erase inbox right after `SDL_DestroyWindow()`. Device get fresh `windowID` each time it reopen window, so without `Forget` stale entries pile up for life of process.

## Why not a shared blocking pump thread

One thread blocking on `SDL_WaitEventTimeout` simpler, but SDL Wayland backend share one `wl_display` connection process-wide. Window destroyed on one thread while other thread block on that connection can deadlock. `SdlEventHub` keep each device's non-blocking, per-render-iteration poll and funnel them through one mutex-serialized drain of real queue. No thread block waiting for event, so that deadlock cannot happen.

## Thread safety

`g_mutex` guard `g_inboxes` and every call into real SDL event functions. `PollEventsFor`'s reference to `g_inboxes[windowID]` stay valid across broadcast loop: `std::unordered_map` invalidate iterators, not element references, on insertion, and broadcast loop only mutate existing elements' vectors, never map's keys.