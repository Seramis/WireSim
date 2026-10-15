#pragma once
#include "../../../Simulator/Platform.hpp"
#include "../Terminal.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>

// WDC W65C51N Asynchronous Communications Interface Adapter (ACIA); see
// docs/w65c51n.pdf and W65C51N.md alongside this file for the register map,
// the PHI2-strobe access model, and everything this model deliberately
// doesn't do (bit-level serial timing, framing/parity, RDR FIFO depth).
class W65C51N final : public Terminal, public Sim::Device {
  public:
	struct W65C51NPins {
		Sim::Bus D;
		Sim::Pin RS0, RS1, RWB, CS0, CS1B, RESB, PHI2;
		Sim::Pin IRQB, RTSB, CTSB, DTRB, DSRB, DCDB;
		Sim::Pin TxD, RxD, RxC, XTLI, XTLO; // vestigial -- see "Byte-level, not bit-level" in W65C51N.md

		W65C51NPins(Sim::Platform* p) :
			D(Sim::Bus::New(p, 8, "W65C51N.D")), RS0(Sim::Pin::New(p, "W65C51N.RS0")),
			RS1(Sim::Pin::New(p, "W65C51N.RS1")), RWB(Sim::Pin::New(p, "W65C51N.RWB")),
			CS0(Sim::Pin::New(p, "W65C51N.CS0")), CS1B(Sim::Pin::New(p, "W65C51N.CS1B")),
			RESB(Sim::Pin::New(p, "W65C51N.RESB")), PHI2(Sim::Pin::New(p, "W65C51N.PHI2")),
			IRQB(Sim::Pin::New(p, "W65C51N.IRQB")), RTSB(Sim::Pin::New(p, "W65C51N.RTSB")),
			CTSB(Sim::Pin::New(p, "W65C51N.CTSB")), DTRB(Sim::Pin::New(p, "W65C51N.DTRB")),
			DSRB(Sim::Pin::New(p, "W65C51N.DSRB")), DCDB(Sim::Pin::New(p, "W65C51N.DCDB")),
			TxD(Sim::Pin::New(p, "W65C51N.TxD")), RxD(Sim::Pin::New(p, "W65C51N.RxD")),
			RxC(Sim::Pin::New(p, "W65C51N.RxC")), XTLI(Sim::Pin::New(p, "W65C51N.XTLI")),
			XTLO(Sim::Pin::New(p, "W65C51N.XTLO")) {}
	};
	W65C51NPins Pins;

	W65C51N(Sim::Platform* p);
	~W65C51N();

	void Logic(Sim::TriggerMask mask);
	void Receive(uint8_t b) override;
	void CTS(bool clearToSend) override;

  private:
	enum class Trig : uint8_t {
		Reset = 0,
		Immediate = 1,
		Commit = 2
	};

	std::mutex _mutex;
	std::condition_variable _rdrFree; // signalled when RDR empties, on reset and on shutdown
	bool _stopping = false;

	// The terminal's RTS (its clear-to-send for our transmitter); a peer that
	// never announces one is treated as always ready.
	std::atomic<bool> _hostCts{true};
	int8_t _rtsAnnounced = -1; // last RTS state handed to UpdateRTS (-1: none yet)

	// Registers (Control Register bits are stored but have no timing effect
	// -- see "Byte-level, not bit-level")
	uint8_t _commandReg = 0;
	uint8_t _controlReg = 0;
	uint8_t _rdr = 0;
	bool _rdrFull = false;
	bool _irqLatched = false;

	// PHI2-pulse capture, split across an immediate and a stable trigger: the
	// immediate branch latches what the bus is doing while PHI2/selected are
	// high, the stable branch commits exactly once on a settled falling edge.
	bool _pulseActive = false;
	bool _lastIsWrite = false;
	uint8_t _lastAddr = 0;
	uint8_t _lastData = 0;

	// DCDB/DSRB edge tracking for the status-change interrupt (see "Known
	// simplifications" for how this differs from the datasheet's exact
	// freeze-until-status-read behaviour).
	bool _edgeInit = false;
	bool _lastDcd = false;
	bool _lastDsr = false;

	// Everything DriveOutputs() derives the output pins from; unchanged since the
	// last call means every pin write would be a no-op, so the redrive is skipped.
	struct OutputState {
		uint8_t commandReg;
		bool rdrFull, irqLatched;
		int8_t rtsAnnounced; // also moved by Receive() on the host thread
		bool operator==(const OutputState&) const = default;
	};
	std::optional<OutputState> _drivenState;

	// Sim-thread only: whether D currently carries a read, so an idle call can
	// tell there is nothing to release.
	bool _drivingD = false;
	// Set by Receive() (host thread) once it has changed state DriveOutputs
	// derives pins from; cleared by the Logic() call that redrives them.
	std::atomic<bool> _hostDirty{false};

	void ReleaseD() {
		if (_drivingD) {
			Pins.D.Clear();
			_drivingD = false;
		}
	}

	void Reset();
	void DriveRead(uint8_t addr);
	void CommitWrite(uint8_t addr, uint8_t data);
	uint8_t ComputeStatusByte() const;
	void DriveOutputs();
	bool RtsAsserted() const;
};
