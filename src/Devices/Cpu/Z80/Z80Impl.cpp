#include "Z80Impl.hpp"

namespace Z80Impl {

namespace {
struct FrameHeader {
	Cpu* cpu;
	std::size_t prevTop;
};
static_assert(sizeof(FrameHeader) == 16);
} // namespace

void FrameAllocator::operator delete(void* p, std::size_t) noexcept {
	Cpu::FreeFrame(p);
}

void* Cpu::AllocFrame(std::size_t n) {
	const std::size_t total = (n + sizeof(FrameHeader) + 15) & ~std::size_t{15};
	assert(_arenaTop + total <= _arena.size() && "Z80 coroutine arena exhausted");
	auto* header = reinterpret_cast<FrameHeader*>(_arena.data() + _arenaTop);
	header->cpu = this;
	header->prevTop = _arenaTop;
	_arenaTop += total;
	return header + 1;
}

void Cpu::FreeFrame(void* p) noexcept {
	auto* header = static_cast<FrameHeader*>(p) - 1;
	header->cpu->_arenaTop = header->prevTop;
}

void CycleAwait::await_suspend(std::coroutine_handle<> h) noexcept {
	cpu._resume = h;
	cpu._pending = req;
	cpu._pendingValid = true;
}

uint8_t CycleAwait::await_resume() const noexcept {
	return req.kind == Kind::Boundary ? static_cast<uint8_t>(cpu._boundary) : cpu._result;
}

Cpu::Cpu() {
	r.fill(0);
	rAlt.fill(0);
	r[7] = 0xFF; // A: the manual leaves the registers other than PC, I, R undefined after RESET
}

void Cpu::Resume() {
	_pendingValid = false;
	_resume.resume();
}

// ---------------------------------------------------------------------------
// Clock edges
// ---------------------------------------------------------------------------

bool Cpu::Next() {
	// NMI is edge sensitive and latched whenever it falls, not only on a clock edge.
	if (!pins.NMI) {
		if (!_nmiPrevLow) _nmiLatch = true;
		_nmiPrevLow = true;
	} else {
		_nmiPrevLow = false;
	}

	if (pins.CLK == _lastClk) return false;
	_lastClk = pins.CLK;
	if (pins.CLK) OnRise();
	else OnFall();
	return true;
}

void Cpu::EnterReset() {
	_state = State::Reset;
	pc = 0;
	i = 0;
	rr = 0;
	iff1 = iff2 = false;
	im = 0;
	_halted = false;
	_eiDelay = 0;
	_ldAirPending = false;
	_nmiLatch = false;
	_idx = 0;
	_pendingValid = false;
	_stretch = false;
	_ended = false;
	_releaseBus = false;
	_busackRelease = false;
	_resume = {};
	_arenaTop = 0;

	// Address and data bus float; every control output goes inactive.
	pins.aOe = pins.dOe = false;
	pins.ctrlOe = true;
	pins.M1 = pins.MREQ = pins.IORQ = pins.RD = pins.WR = pins.RFSH = pins.HALT = pins.BUSACK = true;
}

void Cpu::LeaveReset() {
	_state = State::Running;
	_nmiLatch = false;
	_resume = Program().handle;
	Resume(); // runs to the first Boundary request
	_boundary = Interrupt::None;
	Resume(); // ... and on to the first opcode fetch
	StartCycle();
}

void Cpu::OnRise() {
	_sampInt = !pins.INT;
	_sampNmi = _nmiLatch;
	_sampBusreq = !pins.BUSREQ;

	if (!pins.RESET) {
		if (_state != State::Reset) EnterReset();
		else if (!pins.ctrlOe) EnterReset(); // very first edge: no outputs driven yet
		return;
	}
	if (_state == State::Reset) {
		LeaveReset();
		return;
	}

	if (_state == State::BusReleased) {
		// BUSREQ is sampled on every rising edge of the granted state; once it is
		// high BUSACK follows on the next falling edge.
		if (pins.BUSREQ) _busackRelease = true;
		return;
	}
	if (_state == State::BusReturning) {
		_state = State::Running;
		StartCycle();
		return;
	}

	if (_ended) {
		_ended = false;
		if (_releaseBus) {
			_releaseBus = false;
			_state = State::BusReleased;
			_busackRelease = false;
			pins.aOe = pins.dOe = pins.ctrlOe = false;
			pins.BUSACK = false;
			return;
		}
		StartCycle();
		return;
	}

	if (_stretch) {
		_stretch = false; // wait state: the logical T-state does not advance
		return;
	}
	++_lt;
	RisingActions();
}

void Cpu::OnFall() {
	switch (_state) {
		case State::Reset:
		case State::BusReturning: return;
		case State::BusReleased:
			if (_busackRelease) {
				_busackRelease = false;
				pins.BUSACK = true;
				_state = State::BusReturning;
			}
			return;
		case State::Running: break;
	}
	if (_ended) return;
	FallingActions();
}

void Cpu::RefreshAddress() {
	pins.Aout = static_cast<uint16_t>(i << 8 | rr);
	pins.aOe = true;
	rr = static_cast<uint8_t>((rr & 0x80) | ((rr + 1) & 0x7F));
}

void Cpu::StartCycle() {
	_cur = _pending;
	_pendingValid = false;
	_lt = 1;
	_stretch = false;
	pins.dOe = false;
	pins.ctrlOe = true;
	pins.MREQ = pins.IORQ = pins.RD = pins.WR = pins.RFSH = true;
	pins.M1 = true;

	switch (_cur.kind) {
		case Kind::Fetch:
			_len = 4 + _cur.n;
			_waitLt = 2;
			break;
		case Kind::IntAck:
			_len = 6 + _cur.n;
			_waitLt = 4;
			break;
		case Kind::MemRead:
		case Kind::MemWrite:
			_len = 3 + _cur.n;
			_waitLt = 2;
			break;
		case Kind::IoRead:
		case Kind::IoWrite:
			_len = 4 + _cur.n;
			_waitLt = 3;
			break;
		case Kind::Idle:
			_len = _cur.n;
			_waitLt = 0;
			break;
		case Kind::Boundary: assert(false); break;
	}

	if (_cur.kind != Kind::Idle) {
		pins.Aout = _cur.addr;
		pins.aOe = true;
	}
	if (_cur.kind == Kind::Fetch || _cur.kind == Kind::IntAck) pins.M1 = false;
	if (_cur.kind == Kind::MemWrite || _cur.kind == Kind::IoWrite) pins.Dout = _cur.data;
}

// Output changes that happen on a rising edge, by cycle kind and logical T-state.
void Cpu::RisingActions() {
	switch (_cur.kind) {
		case Kind::Fetch:
			if (_lt == 3) {
				_result = pins.Din;
				pins.MREQ = pins.RD = pins.M1 = true;
				pins.RFSH = false;
				RefreshAddress();
				Resume(); // decode while T3/T4 refresh the memory
			} else if (_lt == 5) {
				pins.RFSH = true;
			}
			break;
		case Kind::IntAck:
			if (_lt == 5) {
				_result = pins.Din;
				pins.IORQ = pins.M1 = true;
				pins.RFSH = false;
				RefreshAddress();
				Resume();
			} else if (_lt == 7) {
				pins.RFSH = true;
			}
			break;
		case Kind::MemRead: break;
		case Kind::MemWrite:
			if (_lt == 4) pins.dOe = false;
			break;
		case Kind::IoRead:
			if (_lt == 2) pins.IORQ = pins.RD = false;
			break;
		case Kind::IoWrite:
			if (_lt == 2) pins.IORQ = pins.WR = false;
			if (_lt == 5) pins.dOe = false;
			break;
		case Kind::Idle:
		case Kind::Boundary: break;
	}
}

// Output changes that happen on a falling edge, plus WAIT sampling and the end
// of the cycle.
void Cpu::FallingActions() {
	switch (_cur.kind) {
		case Kind::Fetch:
			if (_lt == 1) pins.MREQ = pins.RD = false;
			else if (_lt == 3) pins.MREQ = false; // refresh
			else if (_lt == 4) pins.MREQ = true;
			break;
		case Kind::IntAck:
			if (_lt == 3) pins.IORQ = false;
			else if (_lt == 5) pins.MREQ = false; // refresh
			else if (_lt == 6) pins.MREQ = true;
			break;
		case Kind::MemRead:
			if (_lt == 1) pins.MREQ = pins.RD = false;
			else if (_lt == 3) {
				_result = pins.Din;
				pins.MREQ = pins.RD = true;
			}
			break;
		case Kind::MemWrite:
			if (_lt == 1) {
				pins.MREQ = false;
				pins.dOe = true;
			} else if (_lt == 2) pins.WR = false;
			else if (_lt == 3) pins.MREQ = pins.WR = true;
			break;
		case Kind::IoRead:
			if (_lt == 4) {
				_result = pins.Din;
				pins.IORQ = pins.RD = true;
			}
			break;
		case Kind::IoWrite:
			if (_lt == 1) pins.dOe = true;
			else if (_lt == 4) pins.IORQ = pins.WR = true;
			break;
		case Kind::Idle:
		case Kind::Boundary: break;
	}

	if (_lt == _waitLt && !pins.WAIT) _stretch = true;
	if (_lt == _len && !_stretch) EndOfCycle();
}

// Called on the falling edge of the last T-state of a cycle, once the samples
// taken on that T-state's rising edge are known.
void Cpu::EndOfCycle() {
	if (!_pendingValid) Resume(); // cycles that complete rather than return data mid-way
	if (_pending.kind == Kind::Boundary) {
		_boundary = ResolveInterrupt(_sampBusreq);
		Resume();
	}
	_releaseBus = _sampBusreq;
	_ended = true;
	pins.HALT = !_halted;
}

// An instruction boundary: pick NMI, INT or the next opcode fetch. A bus
// request outranks both and also blocks them for this boundary.
Interrupt Cpu::ResolveInterrupt(bool busRequested) {
	if (_eiDelay > 0) --_eiDelay;
	Interrupt kind = Interrupt::None;
	if (!busRequested) {
		if (_sampNmi) {
			kind = Interrupt::Nmi;
			_nmiLatch = false;
		} else if (_sampInt && iff1 && _eiDelay == 0) {
			kind = Interrupt::Int;
		}
	}
	if (kind != Interrupt::None && _ldAirPending) f = static_cast<uint8_t>(f & ~FlagPV);
	_ldAirPending = false;
	return kind;
}

} // namespace Z80Impl
