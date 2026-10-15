#pragma once
#include "../../../Simulator/Platform.hpp"
#include "../Terminal.hpp"
#include <cstdint>
#include <mutex>

// BusTerminal: a fictional, deliberately simple byte-wide terminal interface
// -- not modeling any real chip. See BusTerminal.md for its CE/OE/WE access
// model and why it exists.
class BusTerminal final : public Terminal, public Sim::Device {
  public:
	struct BusTerminalPins {
		Sim::Bus D;
		Sim::Pin NotCE, NotOE, NotWE;

		BusTerminalPins(Sim::Platform* p) :
			D(Sim::Bus::New(p, 8, "BusTerminal.D")), NotCE(Sim::Pin::New(p, "BusTerminal.NotCE")),
			NotOE(Sim::Pin::New(p, "BusTerminal.NotOE")), NotWE(Sim::Pin::New(p, "BusTerminal.NotWE")) {}
	};
	BusTerminalPins Pins;

	BusTerminal(Sim::Platform* p);

	void Logic(Sim::TriggerMask mask);
	void Receive(uint8_t b) override;

  private:
	enum class Trig : uint8_t {
		NotCE = 0,
		NotOE = 1,
		NotWE = 2
	};

	std::mutex _mutex;

	// Single-byte receive buffer. RTS/CTS flow control (see Logic()/Receive())
	// keeps a connected peer from overrunning it, so unlike a FIFO it never
	// needs to hold more than one byte -- see BusTerminal.md's "External-side
	// flow control".
	bool _rxFull = false;
	uint8_t _rxByte = 0;

	uint8_t _outputLatch = 0x00;
	bool _lastOutputSelected = false;

	bool _lastIsWrite = false;
	uint8_t _lastWriteData = 0;
};
