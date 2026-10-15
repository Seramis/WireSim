#include "BusTerminal.hpp"

BusTerminal::BusTerminal(Sim::Platform* p) : Pins(p) {
	Pins.NotCE.AddTrigger(this, Trig::NotCE);
	Pins.NotOE.AddTrigger(this, Trig::NotOE);
	Pins.NotWE.AddTrigger(this, Trig::NotWE, false);

	UpdateRTS(true); // announce readiness in case a peer is already listening; a no-op otherwise
}

void BusTerminal::Logic(Sim::TriggerMask mask) {
	std::lock_guard<std::mutex> lock(_mutex);

	const bool selected = Pins.NotCE.IsLow();
	const bool outputSelected = selected && Pins.NotOE.IsLow();
	const bool isWrite = selected && Pins.NotWE.IsLow();

	// Read: a falling edge of NotOE (newly asserted while NotCE is already
	// low) pops the buffered byte -- or 0x00 if none has arrived -- into the
	// output latch, which then stays on D for as long as this same assertion
	// holds. Comparing against the last-seen level (not the trigger mask)
	// makes this safe to re-run on every settle iteration without
	// double-popping, the same idempotency argument W65C51N's DCD/DSR edge
	// tracking relies on.
	if (outputSelected && !_lastOutputSelected) {
		_outputLatch = _rxFull ? _rxByte : 0x00;
		_rxFull = false;
	}
	_lastOutputSelected = outputSelected;

	// Write: data is captured on NotWE's own stable trigger (so the bus has
	// fully settled) while it's asserted, then committed to the terminal's
	// TX stream once NotWE is released -- the same capture-on-assert,
	// commit-on-release split ParallelSram uses for its write cycle.
	const bool isWriteDone = !isWrite && _lastIsWrite;
	_lastIsWrite = isWrite;
	if (isWriteDone) Send(_lastWriteData);

	if (isWrite && mask.IsSet(Trig::NotWE)) {
		_lastWriteData = Pins.D.Read<uint8_t>();
	}

	if (outputSelected) Pins.D.Write(_outputLatch);
	else Pins.D.Clear();

	// External-side flow control: tell any connected Terminal peer (e.g.
	// TcpTerminal over a NullModemCable) whether the single-byte buffer has
	// room. Recomputed and announced on every Logic() call rather than only
	// on a fullness transition -- unlike ST16C550's event-gated
	// UpdateFlowControl(), this device has no register write or reset pin of
	// its own to serve as a guaranteed first announce once a peer connects,
	// so the next bus access (any NotCE/NotOE/NotWE change) doubles as that
	// announce. The call itself is cheap (see Receive() below) and Logic()
	// only runs on an actual pin change, never on a free-running clock, so
	// this isn't a hot path.
	UpdateRTS(!_rxFull);
}

void BusTerminal::Receive(uint8_t b) {
	std::lock_guard<std::mutex> lock(_mutex);
	if (_rxFull) return; // shouldn't happen: RTS is deasserted whenever the buffer is full
	_rxByte = b;
	_rxFull = true;
	UpdateRTS(false); // announce immediately rather than waiting for the next bus access
}
