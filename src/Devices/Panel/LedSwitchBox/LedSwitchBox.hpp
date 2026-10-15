#pragma once
#include "../../../Simulator/Platform.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// LED strip + switchbox: an 8-bit readable/writeable I/O port with
// D/NotCE/NotWE/NotOE control lines. A write latches onto 8 LEDs; a read
// drives D from 8 switches. See LedSwitchBox.md for the window/rendering
// model -- a dedicated SDL window, own thread, own subsystem refcount.
class LedSwitchBox final : public Sim::Device {
  public:
	struct LedSwitchBoxPins {
		Sim::Bus D;
		Sim::Pin NotCE, NotWE, NotOE;
	};
	LedSwitchBoxPins Pins;

	LedSwitchBox(Sim::Platform* p, std::string name = "LedSwitchBox");
	~LedSwitchBox();

	void Logic(Sim::TriggerMask mask);

	// Opens the SDL window if it isn't already open; no-op otherwise. Safe
	// to call from any thread (does not touch Pins/nets).
	void OpenWindow();

  private:
	enum class CTRL : uint8_t {
		NotCE = 0,
		NotOE = 1,
		NotWE = 2
	};

	Sim::Diagnostics& _diagnostics;
	std::string _name;

	// Last value written (drives the LEDs) and the switch positions (drive
	// D on a read). Plain atomics, not mutex-guarded: each is a single byte
	// written by exactly one side (sim thread for _leds, render/UI thread
	// for _switches) and read by the other, so a relaxed load/store can
	// never see a torn value.
	std::atomic<uint8_t> _leds{0};
	std::atomic<uint8_t> _switches{0};

	std::thread _renderThread;
	std::atomic<bool> _stopRequested{false};
	std::atomic<bool> _windowOpen{false};
	bool _openRequested = false;
	std::mutex _openMutex;
	std::condition_variable _openCv;

	void RenderLoop();
};
