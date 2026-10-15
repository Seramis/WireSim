#include "ST16C550.hpp"

ST16C550::ST16C550(Sim::Platform* p) : Pins(p) {
	Pins.CS0.AddTrigger(this, LogicBit::ACTION);
	Pins.CS1.AddTrigger(this, LogicBit::ACTION);
	Pins.NotCS2.AddTrigger(this, LogicBit::ACTION);
	Pins.IOR.AddTrigger(this, LogicBit::ACTION);
	Pins.NotIOR.AddTrigger(this, LogicBit::ACTION);
	Pins.IOW.AddTrigger(this, LogicBit::ACTION);
	Pins.NotIOW.AddTrigger(this, LogicBit::ACTION);
	Pins.RESET.AddTrigger(this, LogicBit::RST);

	_timerThread = std::thread(&ST16C550::TimerLoop, this);
}

ST16C550::~ST16C550() {
	_timerRunning = false;
	if (_timerThread.joinable()) _timerThread.join();
}

void ST16C550::TimerLoop() {
	while (_timerRunning) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		std::lock_guard<std::mutex> lock(_uartMutex);
		if (IsFifoEnabled() && !_rxFifo.empty()) {
			_timeoutOccurred = true;
			UpdateInterrupts(true);
		}
	}
}

void ST16C550::Logic([[maybe_unused]] const Sim::TriggerMask mask) {
	// Level-sensitive, like the real chip's MR pin: held in reset for as
	// long as RESET reads high, not merely on whichever call happens to
	// carry the RST trigger bit. Checking the bit instead of the pin's
	// actual level let a register access coincide with an unrelated RESET
	// touch (same net, same Logic() call -- Platform batches every fired
	// trigger bit for a device into one mask) and get silently dropped by
	// this `return`, since the net doesn't have to have been asserted for
	// its trigger to fire. See src/Testing/DeviceHarness.md.
	if (Pins.RESET.IsHigh()) {
		Reset();
		return;
	}

	std::lock_guard<std::mutex> lock(_uartMutex);

	const auto selected = Pins.CS0.IsHigh() && Pins.CS1.IsHigh() && Pins.NotCS2.IsLow();
	const auto isRead = selected && (Pins.IOR.IsHigh() || Pins.NotIOR.IsLow());
	const auto isWrite = selected && (Pins.IOW.IsHigh() || Pins.NotIOW.IsLow());
	const auto isWriteStroke = isWrite && !_lastIsWrite;
	_lastIsWrite = isWrite;

	if (!isRead) Pins.D.Clear();
	if (!selected) return;

	uint8_t addr = Pins.A.Read<uint8_t>();
	if (isRead) {
		switch (addr) {
			case 0: IsDlab() ? ReadDLL() : ReadRHR(); break;
			case 1: IsDlab() ? ReadDLM() : ReadIER(); break;
			case 2: ReadISR(); break;
			case 3: ReadLCR(); break;
			case 4: ReadMCR(); break;
			case 5: ReadLSR(); break;
			case 6: ReadMSR(); break;
			case 7: ReadSPR(); break;
		}
	} else if (isWriteStroke) {
		switch (addr) {
			case 0: IsDlab() ? WriteDLL() : WriteTHR(); break;
			case 1: IsDlab() ? WriteDLM() : WriteIER(); break;
			case 2: WriteFCR(); break;
			case 3: WriteLCR(); break;
			case 4: WriteMCR(); break;
			case 7: WriteSPR(); break;
		}
	}
}

void ST16C550::UpdateInterrupts(bool fromTimer) {
	_interruptPending = false;
	const auto triggerLevel = GetTriggerLevel();

	// RX Data / Timeout
	if (_ier & 0x01) {
		if ((int)_rxFifo.size() >= (fromTimer ? 1 : triggerLevel) || _timeoutOccurred) {
			_interruptPending = true;
		}
	}
	// THR Empty
	if ((_ier & 0x02) && true) { // Simulating THR always empty after Send
		_interruptPending = true;
	}

	Pins.INT.Write(_interruptPending ? Sim::LogicState::H : Sim::LogicState::L);
}

void ST16C550::ReadISR() {
	uint8_t isr = _interruptPending ? 0x00 : 0x01;

	if ((_ier & 0x01) && _timeoutOccurred) {
		isr = 0x0C; // Timeout status
		_timeoutOccurred = false;
	} else if ((_ier & 0x01) && (int)_rxFifo.size() >= GetTriggerLevel()) {
		isr = 0x04; // Received Data Available
	} else if (_ier & 0x02) {
		isr = 0x02; // THR Empty
	}

	if (IsFifoEnabled()) isr |= 0xC0;
	Pins.D.Write(isr);
	UpdateInterrupts();
}

void ST16C550::Receive(uint8_t b) {
	std::lock_guard<std::mutex> lock(_uartMutex);
	// Real chip: "the receiver SIN is disconnected" while LOOP is set, so
	// nothing arriving from the external line reaches the FIFO -- only a
	// loopback WriteTHR() (below) feeds it.
	if (IsLoopback()) return;
	PushRxByte(b);
	UpdateInterrupts();
	UpdateFlowControl();
}

void ST16C550::PushRxByte(uint8_t b) {
	if (_rxFifo.size() < 16) _rxFifo.push_back(b);
	else _overrunError = true; // matches real chip: byte is lost, existing FIFO contents kept
}

void ST16C550::ReadRHR() {
	if (!_rxFifo.empty()) {
		Pins.D.Write(_rxFifo.front());
		_rxFifo.pop_front();
	}
	_timeoutOccurred = false;
	UpdateInterrupts();
	UpdateFlowControl();
}

void ST16C550::WriteTHR() {
	const auto b = Pins.D.Read<uint8_t>();
	if (IsLoopback()) {
		// "The output of the TSR is looped back into the receiver shift
		// register input" -- the byte never reaches Send()/the external line.
		PushRxByte(b);
		UpdateFlowControl();
	} else {
		Send(b); // Terminal class worker picks this up
	}
	UpdateInterrupts();
}

void ST16C550::Reset() {
	std::lock_guard<std::mutex> lock(_uartMutex);
	_ier = _fcr = _lcr = _mcr = 0;
	_rxFifo.clear();
	_overrunError = false;
	_hwFlowStop = false;
	_timeoutOccurred = false;
	Pins.D.Clear();
	Pins.INT.Write(Sim::LogicState::F);
	UpdateInterrupts();
	UpdateFlowControl(); // MCR reset to 0 -- RTS deasserted until software asserts it
}

int ST16C550::GetTriggerLevel() const {
	if (!IsFifoEnabled()) return 1;
	switch (_fcr >> 6) {
		case 1: return 4;
		case 2: return 8;
		case 3: return 14;
		default: return 1;
	}
}

// Boilerplate registers
void ST16C550::ReadIER() {
	Pins.D.Write(_ier);
}
void ST16C550::WriteIER() {
	_ier = Pins.D.Read<uint8_t>() & 0x0F;
	UpdateInterrupts();
}
void ST16C550::ReadLCR() {
	Pins.D.Write(_lcr);
}
void ST16C550::WriteLCR() {
	_lcr = Pins.D.Read<uint8_t>();
}
void ST16C550::WriteFCR() {
	_fcr = Pins.D.Read<uint8_t>();
	if (_fcr & 0x02) {
		_rxFifo.clear();
		_overrunError = false;
		UpdateFlowControl();
	}
}
void ST16C550::ReadLSR() {
	uint8_t l = 0x60;
	if (!_rxFifo.empty()) l |= 0x01;
	if (_overrunError) {
		l |= 0x02;
		_overrunError = false;
	} // error bits clear on read, matching the real chip
	Pins.D.Write(l);
}
void ST16C550::ReadMSR() {
	if (IsLoopback()) {
		// "The four modem control outputs are internally connected to the
		// four modem control inputs": CTS/DSR/RI/DCD (bits 4-7) mirror
		// RTS/DTR/OP1/OP2 (MCR bits 1/0/2/3). This model has no real DTR/OP1/
		// OP2 pins or delta-bit tracking either way (see "Known
		// simplifications"), so bits 0-3 stay 0 regardless of loop mode.
		uint8_t msr = 0;
		if (_mcr & 0x02) msr |= 0x10; // CTS <- RTS
		if (_mcr & 0x01) msr |= 0x20; // DSR <- DTR
		if (_mcr & 0x04) msr |= 0x40; // RI  <- OP1
		if (_mcr & 0x08) msr |= 0x80; // DCD <- OP2
		Pins.D.Write(msr);
		return;
	}
	Pins.D.Write(0xB0);
}
void ST16C550::ReadSPR() {
	Pins.D.Write(_spr);
}
void ST16C550::WriteSPR() {
	_spr = Pins.D.Read<uint8_t>();
}
void ST16C550::ReadDLL() {
	Pins.D.Write(_dll);
}
void ST16C550::WriteDLL() {
	_dll = Pins.D.Read<uint8_t>();
}
void ST16C550::ReadDLM() {
	Pins.D.Write(_dlm);
}
void ST16C550::WriteDLM() {
	_dlm = Pins.D.Read<uint8_t>();
}

// MCR bits: 0=DTR, 1=RTS, 2=OP1, 3=OP2, 4=LOOP (see WriteTHR/Receive/ReadMSR/UpdateFlowControl, and ST16C550.md's
// "Loopback" section)
void ST16C550::ReadMCR() {
	Pins.D.Write(_mcr);
}
void ST16C550::WriteMCR() {
	_mcr = Pins.D.Read<uint8_t>() & 0x1F;
	UpdateFlowControl();
}

// Effective RTS = software's request (MCR bit 1) AND the FIFO having room. A plain
// ST16C550 has no auto-flow silicon -- RTS is purely software-driven -- but this
// simulator delivers RX bytes at host-thread speed over a byte-stream link (see
// ST16C550.md's "Byte-level, not bit-level"), which can far outrun the *simulated*
// CPU polling its own buffer occupancy (see kernel.s's uart_rts_update). Gating RTS
// here too, synchronously as bytes land in the FIFO, is what actually keeps pace
// with the sender; firmware's own RTS management remains a second, slower-reacting
// layer on top, matching how real drivers manage flow control from their own buffer.
void ST16C550::UpdateFlowControl() {
	if (_rxFifo.size() >= 4) _hwFlowStop = true;
	else if (_rxFifo.size() <= 1) _hwFlowStop = false;
	// "The four modem control outputs are forced to the inactive (high)
	// levels" while LOOP is set -- i.e. deasserted to any real peer,
	// regardless of what software has written to MCR.RTS.
	UpdateRTS(!IsLoopback() && ((_mcr & 0x02) != 0) && !_hwFlowStop);
}