#include "SdlEventHub.hpp"
#include <mutex>
#include <unordered_map>

namespace {
std::mutex g_mutex;
std::unordered_map<Uint32, std::vector<SDL_Event>> g_inboxes;

// 0 means "no single owner" (e.g. SDL_QUIT) -- broadcast to every inbox.
Uint32 WindowIdOf(const SDL_Event& event) {
	switch (event.type) {
		case SDL_WINDOWEVENT: return event.window.windowID;
		case SDL_KEYDOWN:
		case SDL_KEYUP: return event.key.windowID;
		case SDL_TEXTINPUT: return event.text.windowID;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: return event.button.windowID;
		case SDL_MOUSEMOTION: return event.motion.windowID;
		case SDL_MOUSEWHEEL: return event.wheel.windowID;
		default: return 0;
	}
}
} // namespace

namespace SdlEventHub {
std::vector<SDL_Event> PollEventsFor(Uint32 windowID) {
	std::lock_guard lock(g_mutex);
	auto& mine = g_inboxes[windowID]; // registers windowID's inbox if new

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		const Uint32 target = WindowIdOf(event);
		if (target == 0) {
			for (auto& [id, inbox] : g_inboxes)
				inbox.push_back(event);
		} else {
			g_inboxes[target].push_back(event);
		}
	}

	std::vector<SDL_Event> result;
	result.swap(mine);
	return result;
}

void Forget(Uint32 windowID) {
	std::lock_guard lock(g_mutex);
	g_inboxes.erase(windowID);
}
} // namespace SdlEventHub
