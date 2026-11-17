---
name: device-harness
description: Exercise a single Sim::Device in isolation with DeviceHarness, driving its pins/bus directly via a text script -- no CPU, ROM, or other devices needed. Use to verify a new or changed device, or to reproduce a register/pin-level bug. This is the standard, sufficient way to verify a device -- no machine involved.
---
# Exercise a device standalone with DeviceHarness

`DeviceHarness` separate CMake target. Run one `.devtest` script against exactly one device, drive and read pins direct. Single-device only — script no wire two devices together. Design reason in `src/Testing/DeviceHarness.md`; read once for test-point model and setup-then-run contract. This skill = quick workflow.

## 1. Build it

```bash
cmake --preset debug               # only needed after adding a new .harness.cpp
cmake --build --preset debug --target DeviceHarness
build/debug/DeviceHarness --list-devices   # confirm your device is registered
```

## 2. Register the device (once per device)

Add colocated `<Name>.harness.cpp`. It construct real device, wrap each pin with `Sim::Testing::MakeStub`, self-register:

```cpp
#include "../../../Testing/HarnessCore.hpp"   // from the standard Category/MyChip/ depth
#include "MyChip.hpp"

namespace {

Sim::Testing::DeviceHandle CreateMyChip(Sim::Platform& p, const Sim::Testing::ArgsMap& args) {
	auto dev = std::make_unique<MyChip>(&p);

	Sim::Testing::DeviceHandle h;
	h.Points.emplace_back("D", Sim::Testing::MakeStub(p, dev->Pins.D, "D"));
	h.Points.emplace_back("NotCE", Sim::Testing::MakeStub(p, dev->Pins.NotCE, "NotCE"));
	// ... one line per pin in Pins, in struct order ...
	// Only if MyChip owns an SDL window and inherits SdlUiTestHooks
	// (src/Support/SdlUiTestHooks.hpp, which provides CaptureScreenshot()/Click();
	// only OpenWindow() is the device's own). Omit both lines otherwise.
	dev->OpenWindow();
	h.Ui = dev.get();
	h.Owner = std::make_unique<Sim::Testing::OwnedDevice<MyChip>>(std::move(dev));
	return h;
}

} // namespace

HARNESS_REGISTER_DEVICE("MyChip", CreateMyChip)   // no trailing semicolon
```

`args` = map from script's `device ... key=value` tokens. Use `.AsInt()`, `.AsString()` or `.LoadBytes()` (for leading `@path`) to turn token into whatever real constructor need.

## 3. Write a script

```
device MyChip
drive NotCE = L
drive NotWE = L
drive D = 0x3C
step
clear D          # release before expecting the device to drive it back
drive NotWE = H
step
expect D = 0x3C
```

- One `device <Type> [key=value...]` line per script, before any `drive`/`step`/`expect`/`dump`. Pins referenced by bare name.
- Values: `H`/`L`/`Hw`/`Lw`/`F` for pin, `0x..` or decimal for bus (state name against bus = every bit).
- `step [count]` advance that many cycles. Clock just another pin; drive it explicit.
- Report lines tagged `[s=<n> t=<ms>]`: cycle count and real milliseconds since run phase began. Diverge only around `sleep` or device's own background thread.
- `sleep <milliseconds>` block real time, for device that advance state from background thread. Put plain `step` right after `sleep` before fresh read, queued work drain one cycle late. See `DeviceHarness.md`'s `sleep` note, and "Verifying real-time or background-thread behavior" for when script wrong tool.
- **Pin written only from stable-triggered branch not visible until one cycle after edge that changed it.** Hold for any device. `expect` it one `step` later. See `DeviceHarness.md`'s "Stable writes lag one cycle".
- **Macros:** `def <name> ... end` defines macro; line starting with its name is call, `$0`, `$1`... in body are call's arguments. Script repeating few lines with different values should define one: register write or read access, reset/init sequence, clock pulse, bus cycle, group of `expect`s. Define at top, name for operation, script becomes list of operations. Rules in `DeviceHarness.md`'s "Macros" (exact argument count, `$$` for literal `$`, define before use, no built-in names).
- `print <text>` echo a line, for greppable section tags in committed scripts.
- **To advance clock with bus idle** (background tick, free-running counter), make bus really idle, not just unaddressed. Switch to read mode (or unused register address) before block of bare clock toggles, else stale pending write recommit on every toggle.
- **First `step` absorb setup residue.** Every linked pin start dirty, first cycle resolve them all together. Immediate triggers fire whenever their net is *resolved*, not only on value change, so handler that treat "my trigger bit fired" as proof signal changed can batch with unrelated register-access trigger on that first settle. When registering device, check trigger handlers read pin's actual level.
- **Always `clear` a bus you drove before expecting device to drive it back.** Stub still driving bus fight device, harness print `[s=N t=M] CONTENTION ...` after `step`. Check output for that line instead of relying on value "looking wrong". Applies again when pin's role change mid-script (input under one mode setting that become device-driven under another): `clear` it where mode switch commit, not only at setup.
- Device that inherit `SdlUiTestHooks` get two more verbs: `click <x> <y> [left|right|middle]` (real synthetic click routed through device's SDL event handling; pair with pin-level `expect`) and `screenshot <path>` (save frame as `.bmp` for human review, not pass/fail check). See `DeviceHarness.md`'s "UI devices".

## 4. Run it and keep the regression test

```bash
build/debug/DeviceHarness -v src/Devices/<Category>/<Name>/<Name>.devtest
```

Exit code: 0 all `expect`s passed, 1 some failed, 2 malformed script. `-v` turn on `Diagnostics::debugMode`, show debug prints device gates on it.

Commit working script, macros included, as `<Name>.devtest` next to device. Become standing regression test. For one-off exploration, pipe script on stdin with `-` as path:

```bash
build/debug/DeviceHarness -v - <<'EOF'
device MyChip
drive NotCE = L
step
expect D = 0x3C
EOF
```

## Devices that drive the bus (CPU, DMA)

Bus master needs no special support, but script written other way round: script is the memory, each bus cycle is macro call feeding data and asserting address and control lines. See [bus-master.md](bus-master.md) for bus profile, cycle macros, register readout, failure classification.

## Where this fits

This = verification step for the `device` skill. Iteration only rebuild `DeviceHarness`. To reproduce bug in existing device, write focused script that drive suspect pins. Wrong read/write semantics, bad chip-select decode, FIFO or interrupt-priority cases all reachable this way. Symptom that only exist through CPU/bus interaction, or through how device's decode behave next to another device or shared bus decoder, = machine concern outside this skill; say so instead of chasing it.