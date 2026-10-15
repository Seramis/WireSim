#include "W65C51N.hpp"

W65C51N::W65C51N(Sim::Platform* p) : Pins(p) {
	Pins.RESB.AddTrigger(this, Trig::Reset);
	Pins.CS0.AddTrigger(this, Trig::Immediate);
	Pins.CS1B.AddTrigger(this, Trig::Immediate);
	Pins.RS0.AddTrigger(this, Trig::Immediate);
	Pins.RS1.AddTrigger(this, Trig::Immediate);
	Pins.RWB.AddTrigger(this, Trig::Immediate);
	Pins.DCDB.AddTrigger(this, Trig::Immediate);
	Pins.DSRB.AddTrigger(this, Trig::Immediate);
	Pins.PHI2.AddTrigger(this, Trig::Immediate);
	Pins.PHI2.AddTrigger(this, Trig::Commit, false);
}

W65C51N::~W65C51N() {
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_stopping = true;
	}
	_rdrFree.notify_all();
}

void W65C51N::Logic(Sim::TriggerMask mask) {
	// Level-sensitive, matching the real chip's RESB pin: held in reset for as
	// long as RESB reads low, not merely on whichever call happens to carry
	// the Reset trigger bit. Checking the pin's actual level instead of the
	// trigger bit matters because Platform can batch RESB's trigger alongside
	// an unrelated register-access trigger into the same call (e.g. the
	// one-time settle every pin gets from Platform::Link before the first
	// cycle ever runs), and gating on the trigger bit instead of the level
	// would silently drop that register access whenever the two coincide.
	if (Pins.RESB.IsLow()) {
		Reset();
		return;
	}

	const bool selected = Pins.CS0.IsHigh() && Pins.CS1B.IsLow();
	const bool phiHigh = Pins.PHI2.IsHigh();

	// Idle fast path, taken without the lock: no access in progress, D already
	// released, no write waiting to commit, no DCDB/DSRB change to latch, and
	// nothing the host thread changed since the last redrive. Everything else
	// below runs under the lock.
	if (!(selected && phiHigh) && !_drivingD && !_pulseActive && _edgeInit && !_hostDirty.load(std::memory_order_acquire) &&
		Pins.DCDB.IsHigh() == _lastDcd && Pins.DSRB.IsHigh() == _lastDsr) {
		return;
	}

	std::lock_guard<std::mutex> lock(_mutex);
	_hostDirty.store(false, std::memory_order_relaxed);

	const uint8_t addr = (Pins.RS1.IsHigh() ? 2 : 0) | (Pins.RS0.IsHigh() ? 1 : 0);

	if (mask.IsSet(Trig::Immediate)) {
		// Edge-detect only -- comparing against the last seen level makes
		// this safe to re-run on every settle iteration, same idempotency
		// argument as DriveRead's clear-on-read side effects below.
		const bool dcd = Pins.DCDB.IsHigh();
		const bool dsr = Pins.DSRB.IsHigh();
		if (_edgeInit && !(_commandReg & 0x02)) { // IRD=0: DCD/DSR changes enabled
			if (dcd != _lastDcd) _irqLatched = true;
			if (dsr != _lastDsr) _irqLatched = true;
		}
		_lastDcd = dcd;
		_lastDsr = dsr;
		_edgeInit = true;

		if (selected && phiHigh) {
			_lastAddr = addr;
			_lastIsWrite = Pins.RWB.IsLow();
			if (!_lastIsWrite) {
				_drivingD = true;
				DriveRead(addr);
			} else {
				ReleaseD(); // the bus master drives D during a write
				_lastData = Pins.D.Read<uint8_t>();
			}
		} else {
			ReleaseD();
		}
	}

	// Stable: commit a write exactly once, on a fully-settled PHI2 falling
	// edge (see W65C51N.md). A read has no
	// separate commit step: its side effects are applied directly in
	// DriveRead below, since they're idempotent (safe to reapply on every
	// immediate re-evaluation) and that lets a dependent pin (e.g. IRQB
	// clearing on a Status Register read) resolve within the same cycle as
	// the read itself, rather than carrying the one-cycle
	// stable-write-resolves-next-cycle latency a deferred commit would add.
	if (mask.IsSet(Trig::Commit)) {
		if (phiHigh) {
			if (selected) _pulseActive = true;
		} else if (_pulseActive) {
			_pulseActive = false;
			if (_lastIsWrite) CommitWrite(_lastAddr, _lastData);
		}
	}

	DriveOutputs();
}

void W65C51N::DriveRead(uint8_t addr) {
	// Each case writes D from the pre-access state first, then applies that
	// access's clear-on-read side effect (if any). Re-running this several
	// times per settle (the immediate branch can re-fire before the bus
	// settles) is safe: every side effect here only ever sets a flag to
	// false, so re-applying it changes nothing the second time. Contrast
	// with CommitWrite, which must run exactly once per pulse (e.g. Send()
	// actually transmitting a byte) and is deferred to the stable commit
	// below instead.
	switch (addr) {
		case 0:
			Pins.D.Write(_rdr);
			if (_rdrFull) {
				_rdrFull = false;
				_rdrFree.notify_all();
			}
			break;
		case 1:
			Pins.D.Write(ComputeStatusByte());
			_irqLatched = false;
			break;
		case 2: Pins.D.Write(_commandReg); break;
		case 3: Pins.D.Write(_controlReg); break;
	}
}

uint8_t W65C51N::ComputeStatusByte() const {
	uint8_t s = 0x10; // TDRE (bit4) always 1 -- the datasheet's own note: TDR/TSR load together, so this
					  // bit can never be polled to pace writes.
	if (_rdrFull) s |= 0x08;
	// OVRN (bit2) stays 0: Receive() stalls the transport instead of overrunning (see
	// W65C51N.md). FE (bit1) / PE (bit0) stay 0 unconditionally: byte-level model, no framing/parity
	// detection, and the chip never enables parity anyway (see Command Register PME).
	if (Pins.DSRB.IsHigh()) s |= 0x40;
	if (Pins.DCDB.IsHigh()) s |= 0x20;
	if (_irqLatched) s |= 0x80;
	return s;
}

void W65C51N::CommitWrite(uint8_t addr, uint8_t data) {
	switch (addr) {
		case 0: // TDR -- also loads the TSR per the datasheet's TDRE note
			// Real chip: "transmitter is automatically disabled" while CTSB is high. This
			// model has no TSR to stall, so a byte written while CTSB is high (pin, or the
			// terminal's own RTS) is simply dropped rather than queued (see "Known
			// simplifications" in W65C51N.md).
			if (Pins.CTSB.IsLow() && _hostCts) Send(data);
			break;
		case 1: // Programmed Reset (data don't care): clears Command bits 4-0;
				// Control Register and Command bits 7-5 (PMC/PME) are left unchanged.
			_commandReg &= 0xE0;
			break;
		case 2: _commandReg = data; break;
		case 3: _controlReg = data; break;
	}
}

void W65C51N::DriveOutputs() {
	if (_drivenState == OutputState{_commandReg, _rdrFull, _irqLatched, _rtsAnnounced}) return;

	// DTRB/RTSB/IRQB are pure functions of persistent state -- safe to redrive
	// unconditionally on every Logic() call, regardless of which trigger fired it.
	Pins.DTRB.Write(Sim::LogicState::ActiveLow((_commandReg & 0x01) != 0)); // Command bit0 (DTR)

	// RTSB follows Command bits 3-2 (TIC): low for 10/11, high for 00. It is also held high
	// while RDR is full, so the terminal is told to pause as soon as a received byte is
	// waiting -- see W65C51N.md's "Flow control". The same state is announced to the terminal
	// (once per change, so the listener lock stays off the per-cycle path).
	const bool rtsAsserted = RtsAsserted();
	Pins.RTSB.Write(Sim::LogicState::ActiveLow(rtsAsserted));
	if (_rtsAnnounced != static_cast<int8_t>(rtsAsserted)) {
		_rtsAnnounced = static_cast<int8_t>(rtsAsserted);
		UpdateRTS(rtsAsserted);
	}

	// True open-drain, per the datasheet ("permitting several devices to be connected to
	// the common IRQB microprocessor input"): asserts strong low, otherwise floats (F)
	// rather than driving high -- an external pull-up (not modeled here) is what a real
	// board relies on to read idle as high. TriLow, not ActiveLow, is what makes that
	// open-drain behaviour possible: ActiveLow would drive a strong high when deasserted,
	// which would fight another open-drain device sharing this line.
	Pins.IRQB.Write(Sim::LogicState::TriLow(_irqLatched));

	_drivenState = OutputState{_commandReg, _rdrFull, _irqLatched, _rtsAnnounced};
}

bool W65C51N::RtsAsserted() const {
	return (_commandReg & 0x08) != 0 && !_rdrFull;
}

void W65C51N::Reset() {
	std::lock_guard<std::mutex> lock(_mutex);
	_commandReg = 0;
	_controlReg = 0;
	_rdr = 0;
	_rdrFull = false;
	_rdrFree.notify_all();
	_irqLatched = false;
	_pulseActive = false;
	ReleaseD();
	DriveOutputs();
}

void W65C51N::Receive(uint8_t b) {
	std::unique_lock<std::mutex> lock(_mutex);
	// Called on the terminal's RX-drain thread, which may block (see Terminal::RX). Holding the byte
	// here, rather than dropping it as an overrun, backs the sender up through the transport.
	_rdrFree.wait(lock, [this] {
		return !_rdrFull || _stopping;
	});
	if (_stopping) return;

	_rdr = b;
	_rdrFull = true;
	// Receiver interrupt enabled when IRD=0 (Command bit1) and DTR=1 (Command bit0), per
	// the datasheet's Command Register bit1 description.
	if (!(_commandReg & 0x02) && (_commandReg & 0x01)) _irqLatched = true;
	_hostDirty.store(true, std::memory_order_release);
	// RTS falls with RDR full; announce it now instead of waiting for the next Logic() call.
	if (_rtsAnnounced != 0 && !RtsAsserted()) {
		_rtsAnnounced = 0;
		UpdateRTS(false);
	}
	// No direct pin write here: IRQB/RTSB/D are only ever redriven from Logic()
	// (DriveOutputs(), sim thread only), never from this host-thread callback. PHI2 ticks
	// every cycle in any real machine, so the next Logic() call picks this up almost
	// immediately; see W65C51N.md's "Why no direct pin writes from Receive()".
}

void W65C51N::CTS(bool clearToSend) {
	_hostCts = clearToSend;
}
