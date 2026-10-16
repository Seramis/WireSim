#pragma once
#include <array>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace Z80Impl {

// Pin levels as the engine sees them: `true` is a high level, so an active-low
// signal is asserted when its field is `false`.
struct Pins {
	// Inputs, copied in by the device wrapper before every Next().
	bool CLK = false, WAIT = true, INT = true, NMI = true, RESET = true, BUSREQ = true;
	uint8_t Din = 0;

	// Outputs, copied to the real pins by the wrapper after a clock edge.
	uint16_t Aout = 0;
	uint8_t Dout = 0;
	bool aOe = false;	 // address bus driven
	bool dOe = false;	 // data bus driven
	bool ctrlOe = false; // MREQ/IORQ/RD/WR/RFSH driven (released while the bus is granted away)
	bool M1 = true, MREQ = true, IORQ = true, RD = true, WR = true, RFSH = true, HALT = true, BUSACK = true;
};

// Flag register bits (Z80 CPU User Manual, Table 21). Bits 3 and 5 are "not used".
enum Flag : uint8_t {
	FlagC = 0x01,
	FlagN = 0x02,
	FlagPV = 0x04,
	FlagH = 0x10,
	FlagZ = 0x40,
	FlagS = 0x80,
};

// What the CPU decides at an instruction boundary.
enum class Interrupt : uint8_t {
	None,
	Nmi,
	Int
};

// What a suspended instruction is waiting for.
enum class Kind : uint8_t {
	Fetch,	  // M1 opcode fetch (MREQ/RD, refresh in T3-T4)
	IntAck,	  // M1 interrupt acknowledge (IORQ, two automatic wait states)
	MemRead,  // memory read (MREQ/RD)
	MemWrite, // memory write (MREQ/WR)
	IoRead,	  // I/O read (IORQ/RD, one automatic wait state)
	IoWrite,  // I/O write (IORQ/WR, one automatic wait state)
	Idle,	  // internal T-states, no bus activity
	Boundary, // end of instruction: resolve interrupts, then continue
};

struct Request {
	Kind kind = Kind::Boundary;
	uint16_t addr = 0;
	uint8_t data = 0;
	uint8_t n = 0; // extra idle T-states appended to the bus part, or the length of an Idle cycle
};

class Cpu;

// -----------------------------------------------------------------------------
// Coroutine plumbing. Every instruction is written as straight-line code that
// `co_await`s one bus cycle at a time; the engine in Z80Impl.cpp resumes it at
// the clock edge where that cycle's result is available. Frames live in a small
// LIFO arena inside the CPU, so running an instruction never allocates.
// -----------------------------------------------------------------------------
struct FrameAllocator {
	template <class C, class... Args> static void* operator new(std::size_t n, C& cpu, Args&&...) {
		return cpu.AllocFrame(n);
	}
	static void operator delete(void* p, std::size_t) noexcept;
};

class Task {
  public:
	struct promise_type : FrameAllocator {
		Task get_return_object() noexcept {
			return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_always initial_suspend() noexcept {
			return {};
		}
		std::suspend_always final_suspend() noexcept {
			return {};
		}
		void return_void() noexcept {}
		void unhandled_exception() {
			std::terminate();
		}
	};
	std::coroutine_handle<> handle;
	explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
};

template <class T> class SubTask {
  public:
	struct promise_type : FrameAllocator {
		std::coroutine_handle<> continuation;
		T value{};
		SubTask get_return_object() noexcept {
			return SubTask{std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_always initial_suspend() noexcept {
			return {};
		}
		struct Final {
			bool await_ready() const noexcept {
				return false;
			}
			std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
				return h.promise().continuation;
			}
			void await_resume() const noexcept {}
		};
		Final final_suspend() noexcept {
			return {};
		}
		void return_value(T v) noexcept {
			value = v;
		}
		void unhandled_exception() {
			std::terminate();
		}
	};

	explicit SubTask(std::coroutine_handle<promise_type> h) : _h(h) {}
	SubTask(SubTask&& o) noexcept : _h(std::exchange(o._h, {})) {}
	SubTask(const SubTask&) = delete;
	~SubTask() {
		if (_h) _h.destroy();
	}

	bool await_ready() const noexcept {
		return false;
	}
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
		_h.promise().continuation = caller;
		return _h;
	}
	T await_resume() const noexcept {
		return _h.promise().value;
	}

  private:
	std::coroutine_handle<promise_type> _h;
};

template <> class SubTask<void> {
  public:
	struct promise_type : FrameAllocator {
		std::coroutine_handle<> continuation;
		SubTask get_return_object() noexcept {
			return SubTask{std::coroutine_handle<promise_type>::from_promise(*this)};
		}
		std::suspend_always initial_suspend() noexcept {
			return {};
		}
		struct Final {
			bool await_ready() const noexcept {
				return false;
			}
			std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
				return h.promise().continuation;
			}
			void await_resume() const noexcept {}
		};
		Final final_suspend() noexcept {
			return {};
		}
		void return_void() noexcept {}
		void unhandled_exception() {
			std::terminate();
		}
	};

	explicit SubTask(std::coroutine_handle<promise_type> h) : _h(h) {}
	SubTask(SubTask&& o) noexcept : _h(std::exchange(o._h, {})) {}
	SubTask(const SubTask&) = delete;
	~SubTask() {
		if (_h) _h.destroy();
	}

	bool await_ready() const noexcept {
		return false;
	}
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
		_h.promise().continuation = caller;
		return _h;
	}
	void await_resume() const noexcept {}

  private:
	std::coroutine_handle<promise_type> _h;
};

// Suspends the running instruction until the engine has carried out `req`.
struct CycleAwait {
	Cpu& cpu;
	Request req;
	bool await_ready() const noexcept {
		return false;
	}
	void await_suspend(std::coroutine_handle<> h) noexcept;
	uint8_t await_resume() const noexcept;
};

class Cpu {
  public:
	Pins pins;

	Cpu();

	// Called by the device wrapper after it refreshed `pins`. Returns true when
	// CLK changed (so `pins` outputs may have changed too).
	bool Next();

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const;

	// Register file. r[] is indexed like the opcode register field: B C D E H L (unused) A.
	std::array<uint8_t, 8> r{};
	std::array<uint8_t, 8> rAlt{};
	uint8_t f = 0xFF, fAlt = 0;
	uint16_t pc = 0, sp = 0xFFFF, ix = 0, iy = 0;
	uint8_t i = 0, rr = 0; // I and R
	bool iff1 = false, iff2 = false;
	uint8_t im = 0;

	// Called only through FrameAllocator.
	void* AllocFrame(std::size_t n);
	static void FreeFrame(void* p) noexcept;

  private:
	friend struct CycleAwait;

	// ---- Engine (Z80Impl.cpp) ----
	enum class State : uint8_t {
		Reset,
		Running,
		BusReleased,
		BusReturning
	};

	void OnRise();
	void OnFall();
	void EnterReset();
	void LeaveReset();
	void StartCycle();
	void RisingActions();
	void FallingActions();
	void EndOfCycle();
	void Resume();
	Interrupt ResolveInterrupt(bool busRequested);
	void RefreshAddress();
	void Extend(uint8_t n) {
		_len += n;
	}

	State _state = State::Reset;
	bool _lastClk = false;

	Request _cur{};
	Request _pending{};
	bool _pendingValid = false;
	uint8_t _lt = 0;	   // logical T-state within the current cycle (1-based)
	uint8_t _len = 0;	   // logical length of the current cycle
	uint8_t _waitLt = 0;   // logical T-state at whose falling edge WAIT is sampled
	bool _stretch = false; // a wait state follows the current T-state
	bool _ended = false;   // the current cycle finished at the last falling edge
	bool _releaseBus = false;
	bool _busackRelease = false;
	uint8_t _result = 0; // data sampled by the last read-type cycle

	// Inputs as sampled on the most recent rising edge.
	bool _sampInt = false, _sampNmi = false, _sampBusreq = false;
	bool _nmiLatch = false, _nmiPrevLow = false;

	bool _halted = false;
	uint8_t _eiDelay = 0;
	bool _ldAirPending = false; // last instruction was LD A,I / LD A,R
	Interrupt _boundary = Interrupt::None;
	int _idx = 0; // 0: none, 1: IX, 2: IY (index prefix active for the current instruction)

	std::coroutine_handle<> _resume{};

	// Coroutine frame arena (LIFO).
	static constexpr std::size_t ArenaSize = 4096;
	alignas(16) std::array<std::byte, ArenaSize> _arena{};
	std::size_t _arenaTop = 0;

	// ---- Instruction set (Z80Exec.cpp) ----
	Task Program();
	SubTask<void> Execute(uint8_t op);
	SubTask<void> ExecCB();
	SubTask<void> ExecIndexedCB();
	SubTask<void> ExecED();
	SubTask<void> ExecBlock(uint8_t y, uint8_t z);
	SubTask<void> ServiceNmi();
	SubTask<void> ServiceInt();
	SubTask<void> Push16(uint16_t v);
	SubTask<uint16_t> Pop16();
	SubTask<uint16_t> Read16(uint16_t addr);
	SubTask<void> Write16(uint16_t addr, uint16_t v);
	SubTask<uint16_t> FetchOperand16(uint8_t extraHi = 0);
	SubTask<uint16_t> OperandAddress();

	CycleAwait Fetch(uint16_t addr) {
		return {*this, {Kind::Fetch, addr, 0, 0}};
	}
	CycleAwait IntAck(uint16_t addr) {
		return {*this, {Kind::IntAck, addr, 0, 0}};
	}
	CycleAwait Read(uint16_t addr, uint8_t extra = 0) {
		return {*this, {Kind::MemRead, addr, 0, extra}};
	}
	CycleAwait Write(uint16_t addr, uint8_t v, uint8_t extra = 0) {
		return {*this, {Kind::MemWrite, addr, v, extra}};
	}
	CycleAwait In(uint16_t addr) {
		return {*this, {Kind::IoRead, addr, 0, 0}};
	}
	CycleAwait Out(uint16_t addr, uint8_t v) {
		return {*this, {Kind::IoWrite, addr, v, 0}};
	}
	CycleAwait Idle(uint8_t n) {
		return {*this, {Kind::Idle, 0, 0, n}};
	}
	CycleAwait Boundary() {
		return {*this, {Kind::Boundary, 0, 0, 0}};
	}

	// Register helpers.
	uint16_t Pair(std::size_t hi) const {
		return static_cast<uint16_t>(r[hi] << 8 | r[hi + 1]);
	}
	void SetPair(std::size_t hi, uint16_t v) {
		r[hi] = static_cast<uint8_t>(v >> 8);
		r[hi + 1] = static_cast<uint8_t>(v);
	}
	uint16_t HlOrIndex() const {
		return _idx == 0 ? Pair(4) : _idx == 1 ? ix : iy;
	}
	void SetHlOrIndex(uint16_t v) {
		if (_idx == 0) SetPair(4, v);
		else if (_idx == 1) ix = v;
		else iy = v;
	}
	uint16_t Rp(int p) const; // BC DE HL/IX/IY SP
	void SetRp(int p, uint16_t v);
	uint16_t Rp2(int p) const; // BC DE HL/IX/IY AF
	void SetRp2(int p, uint16_t v);
	bool Cond(int cc) const;

	// ALU (Z80Exec.cpp). Each writes f (bits 3 and 5 cleared).
	void Alu(int op, uint8_t v);
	uint8_t Inc8(uint8_t v);
	uint8_t Dec8(uint8_t v);
	uint8_t Rot(int op, uint8_t v);
	void Daa();
	uint16_t Add16(uint16_t a, uint16_t b);
	uint16_t Adc16(uint16_t a, uint16_t b);
	uint16_t Sbc16(uint16_t a, uint16_t b);
	void SetSzp(uint8_t v);
};

} // namespace Z80Impl
