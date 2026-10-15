#pragma once
#include "../../../Simulator/Platform.hpp"
#include <atomic>
#include <optional>
#include <thread>

// WDC W65C22 Versatile Interface Adapter (VIA); see docs/w65c22.pdf and
// W65C22.md alongside this file for the register map, the PHI2-strobe bus
// access model, and the dual timer-clock-source design below.
class W65C22 final : public Sim::Device {
  public:
	struct W65C22Pins {
		Sim::Bus D, PA, PB, RS;
		Sim::Pin CS1, CS2B, RWB, PHI2, RESB, IRQB;
		Sim::Pin CA1, CA2, CB1, CB2;

		W65C22Pins(Sim::Platform* p) :
			D(Sim::Bus::New(p, 8, "W65C22.D")), PA(Sim::Bus::New(p, 8, "W65C22.PA")),
			PB(Sim::Bus::New(p, 8, "W65C22.PB")), RS(Sim::Bus::New(p, 4, "W65C22.RS")),
			CS1(Sim::Pin::New(p, "W65C22.CS1")), CS2B(Sim::Pin::New(p, "W65C22.CS2B")),
			RWB(Sim::Pin::New(p, "W65C22.RWB")), PHI2(Sim::Pin::New(p, "W65C22.PHI2")),
			RESB(Sim::Pin::New(p, "W65C22.RESB")), IRQB(Sim::Pin::New(p, "W65C22.IRQB")),
			CA1(Sim::Pin::New(p, "W65C22.CA1")), CA2(Sim::Pin::New(p, "W65C22.CA2")),
			CB1(Sim::Pin::New(p, "W65C22.CB1")), CB2(Sim::Pin::New(p, "W65C22.CB2")) {}
	};
	W65C22Pins Pins;

	// N vs S selects IRQB's output driver (datasheet 3.5/5.3): N is open-drain,
	// pull-down only, relying on an external pull-up for the high level; S is a
	// full push-pull driver. See W65C22.md's "Variant".
	enum class Variant : uint8_t {
		N,
		S
	};

	// Selects what decrements T1/T2 once per "PHI2 cycle" (datasheet 2.5:
	// T1 "decrements at Phase 2 clock rate"). ClkPin ties it to the real
	// PHI2 pin, so it inherits any jitter in the simulation's own cycle
	// rate. InternalThread instead runs a free-running real-time thread at
	// internalTickHz, decoupling T1/T2 (and PB7's timer-driven output) from
	// simulation speed -- see W65C22.md's "Timer clock source".
	enum class TimerClockSource : uint8_t {
		ClkPin,
		InternalThread
	};

	W65C22(
		Sim::Platform* p,
		Variant variant = Variant::S,
		TimerClockSource timerClockSource = TimerClockSource::ClkPin,
		double internalTickHz = 1'000'000.0
	);
	~W65C22();

	void Logic(Sim::TriggerMask mask);

  private:
	enum class Trig : uint8_t {
		Reset = 0,
		Select = 1, // CS1, CS2B, PHI2, RS, RWB: bus access inputs
		Commit = 2,
		Edge = 3 // CA1, CA2, CB1, CB2, PB6: control-line edge inputs
	};

	enum IfrBit : uint8_t {
		Ca2 = 0,
		Ca1 = 1,
		Sr = 2,
		Cb2 = 3,
		Cb1 = 4,
		Timer2 = 5,
		Timer1 = 6
	};

	Sim::Diagnostics& _diagnostics;
	Sim::Platform* _platform;
	Variant _variant;
	TimerClockSource _timerClockSource;
	double _internalTickHz;

	// Registers -- see W65C22.md's register map table.
	uint8_t _ora = 0, _orb = 0;
	uint8_t _ira = 0, _irb = 0;
	uint8_t _ddra = 0, _ddrb = 0;
	uint16_t _t1Counter = 0;
	uint8_t _t1LatchLow = 0, _t1LatchHigh = 0;
	uint16_t _t2Counter = 0;
	uint8_t _t2LatchLow = 0;
	uint8_t _sr = 0;
	uint8_t _acr = 0, _pcr = 0;
	uint8_t _ifr = 0, _ier = 0;

	// Timer/shift run state, not part of the register map itself.
	bool _t1Armed = true;
	bool _t2Armed = true;
	bool _t1Pb7Level = true; // current level driven on PB7 when T1 owns it
	int _srShiftsDone = 0;
	uint8_t _srT2Divider = 0;
	bool _cb1ClockLevel = true;

	// CA2/CB2 synthesized output levels (handshake/pulse PCR modes); true = high/deasserted.
	bool _ca2Level = true, _cb2Level = true;
	bool _ca2PulseArmed = false, _cb2PulseArmed = false;

	// Control-line edge tracking (CA1/CA2/CB1/CB2/PB6), reset on RESB release.
	bool _edgeInit = false;
	bool _lastCa1 = false, _lastCa2 = false, _lastCb1 = false, _lastCb2 = false, _lastPb6 = false;

	// Bus access bookkeeping for the immediate-capture / stable-commit split.
	bool _pulseActive = false;
	bool _lastIsWrite = false;
	uint8_t _lastAddr = 0, _lastData = 0;

	// Internal timer thread (TimerClockSource::InternalThread only).
	std::thread _timerThread;
	std::atomic<bool> _timerRunning{false};

	// Everything DriveOutputs() derives the output pins from. When none of it has
	// changed since the last call, every Pin::Write would be a no-op, so the
	// whole redrive is skipped.
	struct OutputState {
		uint8_t ora, orb, ddra, ddrb, acr, pcr, ifr, ier;
		bool t1Pb7Level, cb2Level, cb1ClockLevel, ca2Level;
		bool operator==(const OutputState&) const = default;
	};
	std::optional<OutputState> _drivenState;

	// Whether D currently carries a read; an idle call then has nothing to release.
	bool _drivingD = false;

	void ReleaseD() {
		if (_drivingD) {
			Pins.D.Clear();
			_drivingD = false;
		}
	}

	void Reset();
	void DriveRead(uint8_t addr);
	void CommitWrite(uint8_t addr, uint8_t data);
	void DriveOutputs();
	void ProcessControlLineEdges();
	void TimerThreadLoop();

	void Tick(); // one PHI2-equivalent timer step, from either clock source
	void TickT1();
	void TickT2(); // interval mode only; pulse-counting mode ticks from PB6 edges instead
	void TickShiftDivider();
	void ShiftStep(bool isOutput);

	uint8_t Ca2Ctrl() const {
		return (_pcr >> 1) & 0x07;
	}
	uint8_t Cb2Ctrl() const {
		return (_pcr >> 5) & 0x07;
	}
	bool Ca1Positive() const {
		return (_pcr & 0x01) != 0;
	}
	bool Cb1Positive() const {
		return (_pcr & 0x10) != 0;
	}
	bool IsCa2Independent() const {
		return Ca2Ctrl() == 0b001 || Ca2Ctrl() == 0b011;
	}
	bool IsCb2Independent() const {
		return Cb2Ctrl() == 0b001 || Cb2Ctrl() == 0b011;
	}
	uint8_t SrMode() const {
		return (_acr >> 2) & 0x07;
	}
	bool T1FreeRun() const {
		return (_acr & 0x40) != 0;
	}
	bool T1Pb7Enabled() const {
		return (_acr & 0x80) != 0;
	}
	bool T2PulseCounting() const {
		return (_acr & 0x20) != 0;
	}
	bool PaLatchEnabled() const {
		return (_acr & 0x01) != 0;
	}
	bool PbLatchEnabled() const {
		return (_acr & 0x02) != 0;
	}

	void SetIfr(uint8_t bit) {
		_ifr |= (uint8_t)(1 << bit);
	}
	void ClearIfr(uint8_t bit) {
		_ifr &= (uint8_t)~(1 << bit);
	}
	uint8_t ReadIfrByte() const {
		const bool any = (_ifr & _ier & 0x7F) != 0;
		return (uint8_t)(_ifr | (any ? 0x80 : 0));
	}
};
