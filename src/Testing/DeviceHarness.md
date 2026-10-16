# DeviceHarness

Standalone tool. Exercise one `Sim::Device` at a time, via small text script. Drive device pins direct, no CPU, no ROM, no other devices. Wrap `Sim::Platform` own bake-then-run loop, so timing and settle behavior match production. Standard way verify device pin- and register-level correctness.

## Build & run

```bash
cmake --preset debug
cmake --build --preset debug --target DeviceHarness
build/debug/DeviceHarness [-v] <script.devtest>
build/debug/DeviceHarness [-v] -           # read the script from stdin
build/debug/DeviceHarness --list-devices   # registered device type names
```

Exit code: 0 if every `expect` pass, 1 if any fail, 2 for malformed script or CLI usage error. `-v`/`--diag` turn on `Sim::Diagnostics::debugMode`, enable debug prints device gate on it.

Script path `-` read script from stdin, good for throwaway check:

```bash
build/debug/DeviceHarness -v - <<'EOF'
device MyChip
drive NotCE = L
step
expect D = 0x3C
EOF
```

Grammar and output same as file mode. Script worth keep is colocated `<Name>.devtest` file (see "Registering a new device"), because stdin content no re-run later.

`DeviceHarness` separate CMake target. Link engine, device and support sources plus Verilated device libraries, but no machine sources. Entry point `HarnessMain.cpp`. Registration shims (`<Name>.harness.cpp`) excluded from machine binaries.

## Design

### Why the real `Sim::Platform`

Some devices depend on engine immediate/stable settle semantics: write commit on trailing edge of stable trigger, or strobe edge-detect in software instead of re-apply every re-evaluation. Reimplement net resolution outside `Platform` would test against different settling model than production. Harness instead call public `Platform::Start(onBeforeCycle)` loop synchronous on one thread. Script is stimulus, so no `Enqueue` or cross-thread machinery need.

### Why single-device

Script declare exactly one device, reference by bare pin names, no alias, no wiring two devices together. Device own pin and register behavior near all its implementation risk, one device plus script drive its pins cover it. How device behave behind real address bus and shared chip-select decoder need more than one device glue logic. That what `Machine` for (see [MachineHarness.md](MachineHarness.md)). Single-device harness keep grammar and mental model small.

### The test-point model

To let script drive *and* read any pin no matter direction, creating device allocate same-named external stub `Pin`/`Bus` per declared pin and `Platform::Link` it to device pin. Script always act on stub side, never on device own `Pins` member:

- `drive` strongly write stub ("rest of system assert this").
- `clear` release stub to `LogicState::F`, tri-state it so device can drive that bus for read. **Forget this most common mistake.** Stub still driving bus device also drive cause strong-drive fight (`LogicState::E`). See "Contention detection".
- `expect` and `dump` read resolved net value, whoever drive it.

### Setup, then run, and the implicit first `step`

Single `device` line must come before every `drive`/`clear`/`step`/`expect`/`dump` line. `print` exempt, can appear anywhere.

Each pin `device` line create get `Platform::Link`ed to stub during setup, and `Link` always mark its net dirty-immediate, whether or not value changed. Nothing before first `step` run a `Cycle()`, so **first `step` resolve every linked pin together in one pass**. That include whatever device immediate triggers do purely because newly linked.

Harmless for most devices, but check no trigger handler assume "this bit fired" mean "this signal asserted". Immediate triggers fire whenever net *resolved*, not only when value change, so handler should read pin current level when that matter.

### Stable writes lag one cycle

Pin device write only from stable-triggered branch (clock edge, write-capture strobe) not visible through stub until cycle *after* the one that change it (see `overview.md`, phase 2). `expect` on same `step` as causing edge see old value. Add one more `step`:

```
drive CLK = L
step          # the edge that causes the change lands here
step          # one more cycle: now the result is visible
expect OUT = ...
```

This apply to any device with stable-triggered output.

## Script grammar

```
def <name> ... end        # macro, see "Macros"; calls are `<name> <arg0> <arg1> ...`

device <Type> [key=value ...]

drive <pin> = <value>
clear <pin>
step [count]
sleep <milliseconds>
expect <pin> = <value> [message words...]
dump
print <literal text to echo>
click <x> <y> [left|right|middle]
screenshot <path>
dumpregs
expect reg.<name> = <value>
peek <addr>
poke <addr> <value>
expect mem[<addr>] = <value>
```

`click` and `screenshot` need UI device; last five need inspectable device. Both describe below.

- **Values:** `H`/`L`/`Hw`/`Lw`/`F` for single pin (the `Sim::LogicState` names), or number (`0x..` hex or decimal) for bus. Number against single pin is 0/1 shorthand for L/H. State name against *bus* mean every bit (`drive D = F` release whole bus; `expect D = F` check every bit float). No per-bit mixed syntax.
- **`step [count]`** advance `count` (default 1) cycles. No auto-toggle clock: clock just another pin, drive explicit (`drive CLK = H` / `step` / `drive CLK = L` / `step`).
- **`sleep <milliseconds>`** block script for real wall-clock time, run no `Cycle()`. Use for device that advance own state from background thread on real-time schedule, which `step` alone cannot observe. Thread `Platform::Enqueue()` calls queue up during sleep, and *next* `step` drain them all at once. That drain happen after same cycle immediate pass, so register read in that same `step` still see pre-sleep value. Put one plain `step` right after `sleep`, then do read as fresh access:
  ```
  sleep 50
  step
  drive PHI2 = H
  step
  expect D = ...
  ```
  Exact tick count over real sleep depend on host scheduling, so treat as ad-hoc verification, not hard-coded `.devtest` assertion. For cases script cannot reach, see "Verifying real-time or background-thread behavior".
- **`print`** echo its literal text, do nothing else. Use for greppable section tags (`print TAG=...`). Always run in run phase in document order relative to other run commands, so `print` written before `device` line still execute as first run-phase output.
- **`dump`** take no arguments, print every point of device, in order its registration shim declare them (mirror the `Pins` struct).
- **Output** is one line per command (`[s=<n> t=<ms>] drive ...`, `PASS .../FAIL ...`, a `dump` block) plus final `N/M checks passed` line. `s` is cycle count and `t` real milliseconds since run phase begin. They diverge only around `sleep` or device own background thread, which is where `t` useful: a `sleep 50` line `t` jump by 50.
- **Comments:** line start with `#` (after leading whitespace) is comment. Trailing ` # ...` strip from every verb except `print`, which take rest of line literal.

## UI devices (`click`, `screenshot`)

Device that own SDL window can inherit `SdlUiTestHooks` (`src/Support/SdlUiTestHooks.hpp`) alongside `Sim::Device`. It already implement `CaptureScreenshot`/`Click`. Device write only `OpenWindow()` (need anyway) and three one-line calls in render loop; see `SdlUiTestHooks.md`. Script target `click`/`screenshot` against non-UI device fail before anything run, like unknown pin.

Device registration shim open its window unconditional after construct device, so no `open-window` verb:

```cpp
auto dev = std::make_unique<MyChip>(&p);
dev->OpenWindow();      // convention for a UI device's shim
h.Ui = dev.get();       // SdlUiTestHooks*; null (the default) for a non-UI device
```

This cheap for every script because `DeviceHarness` default `SDL_VIDEODRIVER` to `dummy` (in `HarnessMain.cpp`, unless caller set it). Window never visible, no compositor to race screenshot first frame (see `SdlUiTestHooks.md`). To watch script live, set `SDL_VIDEODRIVER=x11` (or `wayland`).

- **`click x y [button]`** synthesize real mouse click at window-logical pixel coordinates, through device actual SDL event path, so device need no change become testable. Pair with `drive`/`expect` on device pins for deterministic check (eg flip switch, then read back over `D`).
- **`screenshot path`** save render frame as `.bmp` (`SDL_SaveBMP`). It **not** pixel-diff check, no `expect-screenshot`: rendering vary across GPU drivers and compositors in way unrelated to device, so pixel check would be flaky. Use for human or CI artifact to look at; use `click` plus `expect` for pass/fail.

## Register/memory inspection

Device can opt into `Sim::IRegisterInspectable` and/or `Sim::IMemoryInspectable` (`src/Simulator/Inspect.hpp`); see `device` skill, step 4. Registration shim set matching capability pointer, as `h.Ui` do for UI device:

```cpp
h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
h.Memory = dynamic_cast<Sim::IMemoryInspectable*>(dev.get());
```

Pointer left null (the default) mean device not implement that interface, and script use matching verb fail at parse time.

- **`dumpregs`** print every `(name, value)` pair from `DumpRegisters()`.
- **`expect reg.<name> = <value>`** assert one named register. Unknown name fail before any cycle run. Take no trailing message.
- **`peek <addr>`** print one byte via `Peek(addr)`. Address check against `InspectableSize()` up front.
- **`poke <addr> <value>`** write one byte via `Poke(addr, value)`. For test setup only.
- **`expect mem[<addr>] = <value>`** assert one byte. Take no trailing message.

These interfaces never call from `Logic()` or hot path; exist for tooling (this harness, `MachineHarness`, debugging), so ordinary virtual dispatch fine.

## Macros

Both harnesses share text pre-pass (`ScriptMacros.hpp`) expanding macros before script parsed, so repeated sequence written once. Use macro whenever script repeats same few lines with different values: register write or read access (strobes, address, data, release), reset or init sequence, clock pulse, bus cycle, group of `expect`s. Name macro for operation, script reads as list of operations:

```
def WRITE_REG            # $0 = register address, $1 = value
  drive RS = $0
  drive D = $1
  drive NotCS = L
  drive NotWR = L
  step
  drive NotWR = H
  drive NotCS = H
  clear D
  step
end

WRITE_REG 0x2 0x80
```

`def <name>` starts macro, `end` closes it; body is ordinary script lines. `$0`, `$1`, ... are call's arguments, counted from 0. Line starting with macro name is call.

- Call replaced by body with each `$N` substituted by N-th argument. Parsers only see expanded, ordinary commands.
- Arguments are whitespace-separated words (no quoting). Call must pass exactly as many as highest `$N` in body plus one.
- `$$` is literal `$`; `$` followed by anything else left alone, so `print` text such as `$FFFC` safe.
- Macro must be defined before call, may call earlier macros (nesting limit 16), cannot be redefined, cannot reuse built-in command name. `def` does not nest.
- Define macros at top of file, before `device` / `machine` line. No effect on setup-then-run ordering: only expanded commands count.
- Errors and report lines refer to line of call.

## Contention detection

After every `step`, harness scan device points for `Sim::LogicState::E` (two conflict strong drivers on one net, almost always missing `clear`) and print each contended pin or bus bit:

```
[s=3 t=0] CONTENTION D bit(s) 0,1,2,3,4,5,6,7 (driven H and L at once -- check for a missing `clear`)
```

Check is per bit via `Bus::operator[]`, not `Bus::Read<uint64_t>()`, which collapse contended bit to 0 and would make mistake look like plausible wrong value. It advisory: not fail script, not change pass count (`expect` on resulting value do that), because script might deliberate exercise bus fight. No `expect pin = E`, since assert contention would test engine drive resolution, not device.

Pin can change role partway through script, eg input under one mode-register setting that become device-driven after setting change. Put `clear` right where mode switch commit, not only at setup. Stub still hold setup-time drive is just as contended.

## Verifying real-time or background-thread behavior

`sleep` cover most real-time case. For what it cannot reach (continuous sampling while real time pass, or state no register expose), link `Sim::Platform` direct in small throwaway program:

```bash
clang++ -I<repo root> -std=c++23 -pthread \
  verify.cpp \
  src/Simulator/Platform.cpp src/Simulator/Pin.cpp src/Simulator/Bus.cpp \
  src/Devices/<Category>/<Name>/<Name>.cpp \
  -o verify
```

```cpp
#include "src/Devices/<Category>/<Name>/<Name>.hpp"
#include <chrono>

int main() {
	Sim::Platform p;
	MyChip dev(&p /*, other constructor args */);
	// Drive every pin to a known level before Start(). There is no stub
	// here, so write the device's own `dev.Pins` members directly.

	auto start = std::chrono::steady_clock::now();
	p.Start([&] {
		auto elapsed = std::chrono::steady_clock::now() - start;
		// Drive or read pins here based on elapsed time. A write takes effect
		// once Cycle() runs after this callback returns; a read sees state as
		// of the end of the PREVIOUS Cycle(). Split a write and its read-back
		// across separate onBeforeCycle calls, not one (the same reason as
		// `sleep`'s drain note).
		if (elapsed > std::chrono::milliseconds(300)) p.Stop();
	});
}
```

Keep program in scratch directory, not in repo. Record what it confirm in device `.md` "Test coverage" section, then discard it.

## Registering a new device

Device registration shim is colocated `<Name>.harness.cpp`. It construct real device, build stub per pin with `Sim::Testing::MakeStub`, and self-register with `HARNESS_REGISTER_DEVICE` (no trailing semicolon, like gtest `TEST(...)`):

```cpp
#include "../../../Testing/HarnessCore.hpp"   // adjust the relative depth to the device's location
#include "MyChip.hpp"

namespace {

Sim::Testing::DeviceHandle CreateMyChip(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	auto dev = std::make_unique<MyChip>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	// ... one line per pin in Pins, in struct order ...
	// Only if MyChip inherits SdlUiTestHooks (see "UI devices"); otherwise omit
	// both lines and h.Ui stays null.
	dev->OpenWindow();
	h.Ui = dev.get();
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<MyChip>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("MyChip", CreateMyChip)
```

`device` line `key=value` tokens reach shim as plain strings in `ArgsMap`. Shim decide what they mean using `ArgValue::AsInt()`, `AsString()` or `LoadBytes()` (leading `@path`; see `HarnessCore.hpp`). Grammar stay fixed while device with richer constructor need, such as ROM take firmware image, add arguments freely:

```
device Rom contents=@firmware.bin size=32768
```

`Sim::Device` has no virtual destructor (see `Device.hpp`), so `DeviceHandle` cannot hold bare `unique_ptr<Sim::Device>`. `Owner` is `unique_ptr<IOwnedDevice>` wrap type-specific `OwnedDevice<T>`, which supply virtual destructor scoped to harness own bookkeeping.

## CPU devices

CPU needs no special support: script drives its clock, reset, interrupt, data pins, expects its address and control pins, reads registers via `dumpregs` / `expect reg.*`. Define one macro per bus-cycle kind (see [Macros](#macros)), write program as list of calls. Method in `.claude/skills/device-harness/bus-master.md` and `device` skill (`cpu.md`); worked example `src/Devices/Cpu/W65C02/W65C02_lda_sta.devtest`.

## Where this fits

This tool is verification step for device work (`device` skill). Not cover multi-device wiring; see "Why single-device".