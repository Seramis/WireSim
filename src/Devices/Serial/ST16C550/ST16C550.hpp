#pragma once
#include "../../../Simulator/Inspect.hpp"
#include "../../../Simulator/Platform.hpp"
#include "../Terminal.hpp"
#include <deque>
#include <mutex>

class ST16C550 final : public Terminal, public Sim::Device, public Sim::IRegisterInspectable {
  public:
	struct ST16C550Pins {
		Sim::Bus A, D;
		Sim::Pin NotIOR, IOR, NotIOW, IOW, CS0, CS1, NotCS2, INT, RESET, RX, TX;

		ST16C550Pins(Sim::Platform* p) :
			A(Sim::Bus::New(p, 3, "A")), D(Sim::Bus::New(p, 8, "D")), NotIOR(Sim::Pin::New(p, "NotIOR")),
			IOR(Sim::Pin::New(p, "IOR")), NotIOW(Sim::Pin::New(p, "NotIOW")), IOW(Sim::Pin::New(p, "IOW")),
			CS0(Sim::Pin::New(p, "CS0")), CS1(Sim::Pin::New(p, "CS1")), NotCS2(Sim::Pin::New(p, "NotCS2")),
			INT(Sim::Pin::New(p, "INT")), RESET(Sim::Pin::New(p, "RESET")), RX(Sim::Pin::New(p, "RX")),
			TX(Sim::Pin::New(p, "TX")) {}
	};
	ST16C550Pins Pins;

  private:
	// State
	std::deque<uint8_t> _rxFifo;
	uint8_t _ier = 0, _fcr = 0, _lcr = 0, _mcr = 0, _spr = 0, _dll = 0, _dlm = 0;
	bool _overrunError = false;
	bool _hwFlowStop = false;

	std::atomic<bool> _timeoutOccurred{false};
	bool _interruptPending = false;
	bool _lastIsWrite = false;

	std::mutex _uartMutex;
	std::thread _timerThread;
	std::atomic<bool> _timerRunning{true};

	enum LogicBit : uint8_t {
		ACTION = 0,
		RST = 1
	};

	void TimerLoop();
	void UpdateInterrupts(bool fromTimer = false);
	void UpdateFlowControl();
	bool IsDlab() const {
		return (_lcr & 0x80) != 0;
	}
	bool IsFifoEnabled() const {
		return (_fcr & 0x01) != 0;
	}
	bool IsLoopback() const {
		return (_mcr & 0x10) != 0;
	}
	int GetTriggerLevel() const;
	void PushRxByte(uint8_t b);

	// Registers
	void ReadRHR();
	void WriteTHR();
	void ReadDLL();
	void WriteDLL();
	void ReadIER();
	void WriteIER();
	void ReadDLM();
	void WriteDLM();
	void ReadISR();
	void WriteFCR();
	void ReadLCR();
	void WriteLCR();
	void ReadLSR();
	void ReadMSR();
	void ReadSPR();
	void WriteSPR();
	void ReadMCR();
	void WriteMCR();

  public:
	ST16C550(Sim::Platform* p);
	virtual ~ST16C550();

	void Logic(const Sim::TriggerMask mask);
	void Receive(uint8_t b) override;
	void Reset();

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return {
			{"IER", _ier},
			{"FCR", _fcr},
			{"LCR", _lcr},
			{"MCR", _mcr},
			{"SPR", _spr},
			{"DLL", _dll},
			{"DLM", _dlm},
			{"RxFifoSize", _rxFifo.size()},
			{"OverrunError", _overrunError},
			{"InterruptPending", _interruptPending},
		};
	}
};
