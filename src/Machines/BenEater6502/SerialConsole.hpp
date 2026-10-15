#pragma once
#include "../../Devices/Serial/TcpTerminal.hpp"
#include "../../Simulator/Platform.hpp"
#include <mutex>

// The host end of the serial line: a TCP server standing in for the terminal
// behind the MAX232. TcpTerminal has a single clear-to-send input, but two
// signals on this machine decide whether the host may send:
//   - the ACIA's RTS, announced through the null-modem link (low at reset, and
//     while RDR still holds an unread byte);
//   - the VIA's PA0, msbasic's software RTS: high while its serial ring buffer
//     is nearly full.
// The host may send only when the ACIA is ready and PA0 is low. This class
// combines the two into the one input TcpTerminal understands.
//
// It is also a Sim::Device, because TcpTerminal cannot watch a pin: `Rts` is the
// net to link to VIA PA0, and its changes are forwarded to SetSoftwareStop().
// CTS() arrives on a terminal thread while Logic() runs on the sim thread, so
// the two flags are guarded by a mutex.
class SerialConsole final : public TcpTerminal, public Sim::Device {
  public:
	Sim::Pin Rts;

	SerialConsole(Sim::Platform* p, std::string name, int port, int backlogSize, uint8_t machineNewline) :
		TcpTerminal(std::move(name), port, backlogSize, machineNewline), Rts(Sim::Pin::New(p, "SerialConsole.Rts")) {
		Rts.AddTrigger(this);
	}

	void Logic(const Sim::TriggerMask) {
		std::lock_guard<std::mutex> lock(_mutex);
		_softwareStop = Rts.IsHigh();
		Forward();
	}

  protected:
	void CTS(bool aciaReady) override {
		std::lock_guard<std::mutex> lock(_mutex);
		_aciaReady = aciaReady;
		Forward();
	}

  private:
	std::mutex _mutex;
	bool _aciaReady = false;
	bool _softwareStop = false;

	// Caller holds _mutex.
	void Forward() {
		TcpTerminal::CTS(_aciaReady && !_softwareStop);
	}
};
