#include "W65C22.hpp"
#include <chrono>

W65C22::W65C22(Sim::Platform* p, Variant variant, TimerClockSource timerClockSource, double internalTickHz) :
	Pins(p), _diagnostics(p->GetDiagnostics()), _platform(p), _variant(variant), _timerClockSource(timerClockSource),
	_internalTickHz(internalTickHz) {
	Pins.RESB.AddTrigger(this, Trig::Reset);
	Pins.CS1.AddTrigger(this, Trig::Select);
	Pins.CS2B.AddTrigger(this, Trig::Select);
	Pins.RWB.AddTrigger(this, Trig::Select);
	Pins.RS.AddTrigger(this, Trig::Select);
	Pins.PHI2.AddTrigger(this, Trig::Select);
	Pins.PHI2.AddTrigger(this, Trig::Commit, false);
	Pins.CA1.AddTrigger(this, Trig::Edge);
	Pins.CA2.AddTrigger(this, Trig::Edge);
	Pins.CB1.AddTrigger(this, Trig::Edge);
	Pins.CB2.AddTrigger(this, Trig::Edge);
	Pins.PB[6].AddTrigger(this, Trig::Edge); // PB6: pulse-counting input for T2

	if (_timerClockSource == TimerClockSource::InternalThread) {
		_timerRunning = true;
		_timerThread = std::thread(&W65C22::TimerThreadLoop, this);
	}
}

W65C22::~W65C22() {
	_timerRunning = false;
	if (_timerThread.joinable()) _timerThread.join();
}

void W65C22::TimerThreadLoop() {
	// Fixed-timestep scheduling: accumulate `next` by the interval instead
	// of resetting it from now() every iteration, so a late wakeup (OS
	// scheduling jitter) is caught up by back-to-back Enqueue() calls
	// rather than permanently losing time -- that's what keeps the
	// long-run average tick rate (and therefore pitch) stable. See
	// W65C22.md's "Timer clock source" for the precision this can and
	// can't guarantee.
	using Clock = std::chrono::steady_clock;
	const auto interval = std::chrono::duration<double>(1.0 / _internalTickHz);
	auto next = Clock::now() + std::chrono::duration_cast<Clock::duration>(interval);

	while (_timerRunning.load(std::memory_order_relaxed)) {
		std::this_thread::sleep_until(next);
		next += std::chrono::duration_cast<Clock::duration>(interval);
		_platform->Enqueue([this] {
			Tick();
		});
	}
}

void W65C22::Logic(Sim::TriggerMask mask) {
	// Level-sensitive: held in reset for as long as RESB reads low, not
	// merely on whichever call happens to carry the Reset trigger bit.
	if (Pins.RESB.IsLow()) {
		Reset();
		return;
	}

	const bool selected = Pins.CS1.IsHigh() && Pins.CS2B.IsLow();
	const bool phiHigh = Pins.PHI2.IsHigh();

	const bool access = selected && phiHigh;

	// Edge tracking must also run on the first call after reset so it can
	// record the pins' starting levels (see ProcessControlLineEdges).
	if (mask.IsSet(Trig::Edge) || !_edgeInit) ProcessControlLineEdges();

	if (mask.IsSetAny(Trig::Select, Trig::Edge)) {
		if (access) {
			_lastAddr = Pins.RS.Read<uint8_t>();
			_lastIsWrite = Pins.RWB.IsLow();
			if (!_lastIsWrite) {
				_drivingD = true;
				DriveRead(_lastAddr);
			} else {
				ReleaseD(); // the bus master drives D during a write
				_lastData = Pins.D.Read<uint8_t>();
			}
		} else {
			ReleaseD();
		}
	}

	// Stable, on PHI2 only: commits a write exactly once on a settled
	// falling edge via the `_pulseActive` latch below, auto-deasserts any
	// armed CA2/CB2 pulse, and -- in ClkPin mode only -- advances T1/T2
	// and the PHI2-rate shift-register sub-modes by one step. In
	// InternalThread mode that last part is instead driven by
	// TimerThreadLoop's own Enqueue()'d Tick() calls, decoupled from PHI2
	// entirely; bus access here is unaffected by the clock-source choice.
	if (mask.IsSet(Trig::Commit)) {
		if (phiHigh) {
			if (selected) _pulseActive = true;
		} else {
			// Tick() runs before this edge's own write commits, not after:
			// datasheet 2.6 says T1 "will start counting down on the next
			// PHI2 clock following the load sequence", i.e. a load and the
			// tick that follows it are different falling edges. Ticking
			// first (against the pre-write state) and only then applying
			// this edge's load keeps that one clock cycle of separation;
			// ticking after would make a T1C-H/T2C-H write immediately
			// consume its own first count.
			if (_timerClockSource == TimerClockSource::ClkPin) Tick();
			if (_pulseActive) {
				_pulseActive = false;
				if (_lastIsWrite) CommitWrite(_lastAddr, _lastData);
			}
			if (_ca2PulseArmed) {
				_ca2Level = true;
				_ca2PulseArmed = false;
			}
			if (_cb2PulseArmed) {
				_cb2Level = true;
				_cb2PulseArmed = false;
			}
		}
	}

	DriveOutputs();
}

void W65C22::ProcessControlLineEdges() {
	const bool ca1 = Pins.CA1.IsHigh();
	const bool ca2 = Pins.CA2.IsHigh();
	const bool cb1 = Pins.CB1.IsHigh();
	const bool cb2 = Pins.CB2.IsHigh();
	const bool pb6 = Pins.PB[6].IsHigh();

	// First settle after construction/reset carries no real edge -- see
	// device-harness's "first step absorbs setup-time residue". _edgeInit
	// defers edge-sensitive work by one call so that settle can't be
	// mistaken for a live transition.
	if (_edgeInit) {
		if (ca1 != _lastCa1 && ca1 == Ca1Positive()) {
			if (PaLatchEnabled()) _ira = Pins.PA.Read<uint8_t>();
			SetIfr(IfrBit::Ca1);
			// Handshake-output CA2: CA1's active transition is the "Data
			// Taken" acknowledgement that deasserts it again (2.4).
			if (Ca2Ctrl() == 0b100) _ca2Level = true;
		}
		const uint8_t ca2Ctrl = Ca2Ctrl();
		if (ca2 != _lastCa2) {
			const bool fires = (ca2Ctrl == 0b000 && !ca2) || (ca2Ctrl == 0b001 && !ca2) || (ca2Ctrl == 0b010 && ca2) ||
							   (ca2Ctrl == 0b011 && ca2);
			if (fires) SetIfr(IfrBit::Ca2);
		}

		const uint8_t srMode = SrMode();
		// In modes 001/010/101/110 the chip drives CB1 as its own shift
		// clock output (see DriveOutputs' srChipClocksCb1); those toggles
		// are the chip's own, not an external device's, so the ordinary PCR
		// interrupt-edge/input-latch role is skipped while they're active.
		// Only mode 000 (ordinary PCR role) and 011/111 (external clock
		// input) treat CB1 as something to react to here.
		const bool cb1IsChipOutput = srMode == 0b001 || srMode == 0b010 || srMode == 0b101 || srMode == 0b110;
		if (!cb1IsChipOutput && cb1 != _lastCb1 && cb1 == Cb1Positive()) {
			if (PbLatchEnabled()) _irb = Pins.PB.Read<uint8_t>();
			SetIfr(IfrBit::Cb1);
			if (Cb2Ctrl() == 0b100) _cb2Level = true;
			// The same CB1 active transition that sets IFR4 also serves as
			// the external shift clock in modes 011/111 (2.12.4/2.13.4).
			if (srMode == 0b011) ShiftStep(false);
			else if (srMode == 0b111) ShiftStep(true);
		}
		// CB2's ordinary PCR interrupt-edge role only applies with the
		// Shift Register disabled (srMode == 0); any active SR mode uses
		// CB2 as its serial data line instead (see DriveOutputs).
		if (cb2 != _lastCb2 && srMode == 0) {
			const uint8_t cb2Ctrl = Cb2Ctrl();
			const bool fires = (cb2Ctrl == 0b000 && !cb2) || (cb2Ctrl == 0b001 && !cb2) || (cb2Ctrl == 0b010 && cb2) ||
							   (cb2Ctrl == 0b011 && cb2);
			if (fires) SetIfr(IfrBit::Cb2);
		}

		if (T2PulseCounting() && pb6 != _lastPb6 && !pb6) TickT2(); // negative-edge pulse count (2.10)
	}

	_lastCa1 = ca1;
	_lastCa2 = ca2;
	_lastCb1 = cb1;
	_lastCb2 = cb2;
	_lastPb6 = pb6;
	_edgeInit = true;
}

void W65C22::DriveRead(uint8_t addr) {
	switch (addr) {
		case 0x0: {
			// Table 2-2: an output-direction bit reads back ORB regardless
			// of pin loading; an input-direction bit reads the latched IRB
			// (if latching enabled) or the live pin level otherwise.
			const uint8_t live = Pins.PB.Read<uint8_t>();
			uint8_t val = 0;
			for (int i = 0; i < 8; i++) {
				const bool bitVal = (_ddrb & (1 << i)) ? ((_orb >> i) & 1)
									: PbLatchEnabled() ? ((_irb >> i) & 1)
													   : ((live >> i) & 1);
				val |= (uint8_t)((bitVal ? 1 : 0) << i);
			}
			Pins.D.Write(val);
			ClearIfr(IfrBit::Cb1);
			if (!IsCb2Independent()) ClearIfr(IfrBit::Cb2);
			break;
		}
		case 0x1:
		case 0xF: {
			// Table 2-3: PA always reflects the latched IRA when latching
			// is enabled, or the live pin level otherwise -- unlike IRB,
			// direction doesn't change which source is read.
			const uint8_t live = Pins.PA.Read<uint8_t>();
			Pins.D.Write(PaLatchEnabled() ? _ira : live);
			if (addr == 0x1) { // $F is explicitly "same as Reg 1 except no Handshake"
				ClearIfr(IfrBit::Ca1);
				if (!IsCa2Independent()) ClearIfr(IfrBit::Ca2);
				const uint8_t ca2Ctrl = Ca2Ctrl();
				if (ca2Ctrl == 0b100) _ca2Level = false; // handshake output: Data Taken asserted
				else if (ca2Ctrl == 0b101) {
					_ca2Level = false;
					_ca2PulseArmed = true;
				}
			}
			break;
		}
		case 0x2: Pins.D.Write(_ddrb); break;
		case 0x3: Pins.D.Write(_ddra); break;
		case 0x4:
			Pins.D.Write((uint8_t)(_t1Counter & 0xFF));
			ClearIfr(IfrBit::Timer1);
			break;
		case 0x5: Pins.D.Write((uint8_t)(_t1Counter >> 8)); break;
		case 0x6: Pins.D.Write(_t1LatchLow); break;
		// Standard 6522 latch-read behaviour (no side effect), not the
		// literal "from T1 high order counter" datasheet wording for this
		// row -- see W65C22.md's "Limitations" for why.
		case 0x7: Pins.D.Write(_t1LatchHigh); break;
		case 0x8:
			Pins.D.Write((uint8_t)(_t2Counter & 0xFF));
			ClearIfr(IfrBit::Timer2);
			break;
		case 0x9: Pins.D.Write((uint8_t)(_t2Counter >> 8)); break;
		case 0xA:
			Pins.D.Write(_sr);
			ClearIfr(IfrBit::Sr);
			_srShiftsDone = 0;
			break;
		case 0xB: Pins.D.Write(_acr); break;
		case 0xC: Pins.D.Write(_pcr); break;
		case 0xD: Pins.D.Write(ReadIfrByte()); break;
		case 0xE: Pins.D.Write((uint8_t)(_ier | 0x80)); break;
	}
}

void W65C22::CommitWrite(uint8_t addr, uint8_t data) {
	_diagnostics.Println("W65C22 write {:X}={:02x}", addr, data);
	switch (addr) {
		case 0x0:
			_orb = data;
			ClearIfr(IfrBit::Cb1);
			if (!IsCb2Independent()) ClearIfr(IfrBit::Cb2);
			{
				const uint8_t cb2Ctrl = Cb2Ctrl();
				if (cb2Ctrl == 0b100) _cb2Level = false;
				else if (cb2Ctrl == 0b101) {
					_cb2Level = false;
					_cb2PulseArmed = true;
				}
			}
			break;
		case 0x1:
			_ora = data;
			ClearIfr(IfrBit::Ca1);
			if (!IsCa2Independent()) ClearIfr(IfrBit::Ca2);
			{
				const uint8_t ca2Ctrl = Ca2Ctrl();
				if (ca2Ctrl == 0b100) _ca2Level = false;
				else if (ca2Ctrl == 0b101) {
					_ca2Level = false;
					_ca2PulseArmed = true;
				}
			}
			break;
		case 0x2: _ddrb = data; break;
		case 0x3: _ddra = data; break;
		case 0x4:
			_t1LatchLow = data;
			break; // "a load to a low order T1 counter is effectively a load to a low order T1 latch" (2.5)
		case 0x5:
			_t1LatchHigh = data;
			_t1Counter = (uint16_t)((_t1LatchHigh << 8) | _t1LatchLow);
			_t1Armed = true;
			ClearIfr(IfrBit::Timer1);
			// "PB7 will go to Logic 0 following the load" for one-shot
			// output (2.6). The datasheet doesn't pin down square wave's
			// starting phase beyond this same load behaviour, so this
			// model starts both the same way -- see W65C22.md.
			if (T1Pb7Enabled()) _t1Pb7Level = false;
			break;
		case 0x6: _t1LatchLow = data; break; // Table 2-7: "no different than a write into the T1 Low Order Register"
		case 0x7:
			_t1LatchHigh = data;
			ClearIfr(IfrBit::Timer1); // Table 2-7: writing T1L-H resets IFR6 even though it doesn't reload the counter
			break;
		case 0x8: _t2LatchLow = data; break;
		case 0x9:
			_t2Counter = (uint16_t)(((uint16_t)data << 8) | _t2LatchLow);
			_t2Armed = true;
			ClearIfr(IfrBit::Timer2);
			break;
		case 0xA:
			_sr = data;
			ClearIfr(IfrBit::Sr);
			_srShiftsDone = 0;
			break;
		case 0xB: _acr = data; break;
		case 0xC: _pcr = data; break;
		case 0xD: _ifr &= (uint8_t)~(data & 0x7F); break; // a logic-1 clears that flag; bit7 is not a real flag
		case 0xE:
			if (data & 0x80) _ier |= (uint8_t)(data & 0x7F);
			else _ier &= (uint8_t)~(data & 0x7F);
			break;
		case 0xF: // "Same as Reg 1 except no Handshake": ORA write, no CA2 pulse/handshake side effect
			_ora = data;
			ClearIfr(IfrBit::Ca1);
			if (!IsCa2Independent()) ClearIfr(IfrBit::Ca2);
			break;
	}
}

void W65C22::Tick() {
	TickT1();
	if (!T2PulseCounting()) TickT2();
	TickShiftDivider();
	DriveOutputs(); // reflect PB7/CB1/CB2 promptly even between bus accesses
}

void W65C22::TickT1() {
	if (_t1Counter == 0)
		_t1Counter = 0xFFFF; // one-shot: keep decrementing past zero, same wraparound 2.9 describes for T2
	else _t1Counter--;

	if (_t1Counter == 0 && _t1Armed) {
		SetIfr(IfrBit::Timer1);
		if (T1Pb7Enabled()) _t1Pb7Level = !_t1Pb7Level;
		if (T1FreeRun()) {
			_t1Counter = (uint16_t)((_t1LatchHigh << 8) | _t1LatchLow); // reload; stays armed, refires every period
		} else {
			_t1Armed = false; // one-shot: no further IFR6 until software reloads T1C-H
		}
	}
}

void W65C22::TickT2() {
	if (_t2Counter == 0) _t2Counter = 0xFFFF;
	else _t2Counter--;

	if (_t2Counter == 0 && _t2Armed) {
		SetIfr(IfrBit::Timer2);
		_t2Armed = false; // T2 has no free-run mode (2.8): always disabled until T2C-H is reloaded
	}
}

void W65C22::TickShiftDivider() {
	const uint8_t mode = SrMode();
	// Modes 001/010/101/110 (T2- or PHI2-clocked) stop after 8 shifts until
	// an SR access re-arms them (2.12.2/2.12.3/2.13.2/2.13.3: "the shifting
	// is disabled" / "clock pulses... will stop"), unlike 100's free-run or
	// 011/111's external-CB1 modes, which the datasheet explicitly says do
	// NOT stop shifting (2.12.4/2.13.4) -- those aren't reached from here at
	// all (CB1-edge-driven, see ProcessControlLineEdges).
	if (mode != 0b100 && _srShiftsDone >= 8) return;

	if (mode == 0b010) { // shift in, PHI2 rate: one shift per tick (2.12.3)
		ShiftStep(false);
		return;
	}
	if (mode == 0b110) { // shift out, PHI2 rate (2.13.3)
		ShiftStep(true);
		return;
	}
	if (mode == 0b001 || mode == 0b101 || mode == 0b100) {
		// T2-rate sub-modes: an independent divider seeded from the T2 low
		// latch, not literally the same hardware counter T2's own interval
		// timer uses -- see W65C22.md's "Limitations".
		if (_srT2Divider == 0) {
			_srT2Divider = _t2LatchLow;
			ShiftStep(mode != 0b001);
		} else {
			_srT2Divider--;
		}
	}
	// 000 disabled, 011/111 external-CB1-clocked: nothing to do per tick.
}

void W65C22::ShiftStep(bool isOutput) {
	if (isOutput) {
		// Bit 7 goes out first and is simultaneously rotated back into bit 0 (2.11 note 1).
		const bool bit7 = (_sr & 0x80) != 0;
		_cb2Level = bit7;
		_sr = (uint8_t)((_sr << 1) | (bit7 ? 1 : 0));
	} else {
		// Bits enter bit 0 first and shift towards bit 7 (2.11 note 2); CB2 is the external data line.
		const bool inBit = Pins.CB2.IsHigh();
		_sr = (uint8_t)((_sr << 1) | (inBit ? 1 : 0));
	}
	_cb1ClockLevel = !_cb1ClockLevel;

	// Free-running T2-rate output (2.13.1) explicitly never stops or sets IFR2.
	if (SrMode() != 0b100 && _srShiftsDone < 8) {
		_srShiftsDone++;
		if (_srShiftsDone == 8) SetIfr(IfrBit::Sr);
	}
}

void W65C22::DriveOutputs() {
	const OutputState state{_ora, _orb, _ddra, _ddrb, _acr, _pcr, _ifr, _ier, _t1Pb7Level, _cb2Level, _cb1ClockLevel, _ca2Level};
	if (_drivenState == state) return;
	_drivenState = state;

	for (int i = 0; i < 8; i++) {
		if (_ddra & (1 << i)) Pins.PA[i].Write(Sim::LogicState::ActiveHigh((_ora >> i) & 1));
		else Pins.PA[i].Write(Sim::LogicState::F);
	}

	for (int i = 0; i < 7; i++) {
		if (_ddrb & (1 << i)) Pins.PB[i].Write(Sim::LogicState::ActiveHigh((_orb >> i) & 1));
		else Pins.PB[i].Write(Sim::LogicState::F);
	}
	// PB7 is still gated by DDRB7 even when T1 owns its level (the timer
	// only reaches the pin through the normal output buffer, same as a
	// real 6522): DDRB7=0 floats it regardless of ACR7.
	if (_ddrb & 0x80) {
		const bool bit7 = T1Pb7Enabled() ? _t1Pb7Level : (((_orb >> 7) & 1) != 0);
		Pins.PB[7].Write(Sim::LogicState::ActiveHigh(bit7));
	} else {
		Pins.PB[7].Write(Sim::LogicState::F);
	}

	const uint8_t srMode = SrMode();
	const bool srActive = srMode != 0;
	const bool srIsOutput = srMode >= 0b100;
	// Chip-generated shift clock on CB1 for the internally-clocked modes;
	// excludes 0b100 deliberately -- see ShiftStep's comment on free-run output.
	const bool srChipClocksCb1 = srMode == 0b001 || srMode == 0b010 || srMode == 0b101 || srMode == 0b110;

	if (srActive) {
		if (srIsOutput) Pins.CB2.Write(Sim::LogicState::ActiveHigh(_cb2Level));
		else Pins.CB2.Write(Sim::LogicState::F); // shift-in: CB2 is the external device's output
	} else {
		switch (Cb2Ctrl()) {
			case 0b100:
			case 0b101: Pins.CB2.Write(Sim::LogicState::ActiveHigh(_cb2Level)); break;
			case 0b110: Pins.CB2.Write(Sim::LogicState::L); break;
			case 0b111: Pins.CB2.Write(Sim::LogicState::H); break;
			default: Pins.CB2.Write(Sim::LogicState::F); break; // input modes: released
		}
	}

	Pins.CB1.Write(srChipClocksCb1 ? Sim::LogicState::ActiveHigh(_cb1ClockLevel) : Sim::LogicState::F);

	switch (Ca2Ctrl()) {
		case 0b100:
		case 0b101: Pins.CA2.Write(Sim::LogicState::ActiveHigh(_ca2Level)); break;
		case 0b110: Pins.CA2.Write(Sim::LogicState::L); break;
		case 0b111: Pins.CA2.Write(Sim::LogicState::H); break;
		default: Pins.CA2.Write(Sim::LogicState::F); break;
	}
	// CA1 is always an input; this chip never drives it.

	const bool irqActive = (_ifr & _ier & 0x7F) != 0;
	// N: open-drain, pull-down only (floats when inactive); S: full push-pull driver (2.14/3.5).
	Pins.IRQB.Write(
		_variant == Variant::N ? Sim::LogicState::TriLow(irqActive) : Sim::LogicState::ActiveLow(irqActive)
	);
}

void W65C22::Reset() {
	_ora = _orb = 0;
	_ira = _irb = 0;
	_ddra = _ddrb = 0; // DDR=0 -> both ports released to input ("all pins placed in the input state", 3.9)
	_acr = _pcr = 0;
	_ifr = _ier = 0;
	_ca2Level = _cb2Level = true;
	_ca2PulseArmed = _cb2PulseArmed = false;
	_pulseActive = false;
	_edgeInit = false; // re-synchronize edge tracking once RESB releases, same reasoning as construction-time settle
	// T1/T2 counters+latches and the Shift Register are explicitly NOT
	// cleared by RESB (3.9: "except T1 and T2 counters and latches, and the
	// SR"); they're simply "disabled from operation" while RESB is low,
	// which falls out naturally here since Logic() returns before Tick()
	// or ShiftStep() can run.
	ReleaseD();
	_diagnostics.Println("W65C22 reset");
	DriveOutputs();
}
