---
name: device-cpp
description: Add, extend, modify, or debug a C++ hardware device (chip, glue logic, peripheral) in the WireSim netlist, verified standalone with DeviceHarness. Use when creating a new Sim::Device, adding pins/registers/triggers to an existing one, or fixing one that misbehaves at the register/pin level.
---
# C++ devices in WireSim

This skill cover life of C++ `Sim::Device`: add one, extend one, fix one. All done through `DeviceHarness`, no CPU, no ROM, no machine. Wire devices into machine separate thing (see last section).

Read `overview.md` first, for trigger and phase meaning. `src/Devices/Memory/Sram/ParallelSram/ParallelSram.hpp` small complete example. `src/Devices/Io/W65C22/W65C22.md` show doc of big device.

## Which case is this?

- **New device:** chip not yet in `src/Devices/`. Follow step 1-5.
- **Extending or modifying:** new register, mode, pin or behavior on chip already there. Follow steps, use each step "Extending" note, skip step 2.
- **Debugging:** chip already there, with wrong value, timing or contention. Go to "Debugging a misbehaving device".

## 1. Understand the chip (or the change)

Spec come as datasheet PDF or text description. Pull same facts either way, before write code.

- **Datasheet PDF** (`docs/<chip>.pdf`): run `pdftotext -layout docs/<chip>.pdf -`. No `-layout`, tables scramble, one item per line. Read table of contents first, go for function description and register/mode tables — that near all behavior model need. Skip DC/AC characteristics, package pinouts, ordering info; those describe silicon, not behavior.
- **Text description:** pull register/pin map, modes, timing from sentences. If sentence no pin down behavior (what reserved bit read back, what happen on same-time read and write), say so in device `.md`, no guess silent.

**Extending:** read device `<Name>.md` first, especially "Internal state reference" and "Limitations", so change fit design already there.

Decide and write down, before code:

- Which registers read/write-asymmetric (write-only, read-only, context-depend like DLAB, reserved on write), and which bits stateful, self-clear or sticky.
- Chip need bit- or cycle-accurate timing, or can model at transaction/register level? `ST16C550.md`'s "Byte-level, not bit-level" show one such tradeoff.
- Anything drive from outside sim thread (byte stream, timer, keyboard)? If yes, route through `Platform::Enqueue()` / `EnqueueWithFuture()` (step 3), not direct write from host thread.
- What you not model, on purpose. Every device here write this down, so decide list up front.
- Order of load and count/shift on same clock edge. Most datasheet put them on different edge. Get it wrong, load eat its own first count. Also check: access register that re-arm free-running process, could let process tick before your check read result.

Chip with big register or mode surface (more than handful register, or several mode; 16C550-class UART or bigger) — no build in one pass. Build register-map skeleton first, every register wired, round-trip readable and writable, check with focused `.devtest` section. Then add one mode or feature at a time, check each. Keep each diff small, easy bisect.

## 2. Place the code (new device only)

- Put machine-agnostic chip in `src/Devices/<Category>/<Name>/<Name>.hpp`, plus `.cpp` if need. Every device get own folder, soon get `.md`, `.harness.cpp`, `.devtest` too (see `AGENTS.md` repository map for categories).
- New `.cpp` file pick up by `CMakeLists.txt` glob after re-configure: `cmake --preset debug`.

## 3. Implement

```cpp
#pragma once
#include "../../../Simulator/Platform.hpp"

class MyChip final : public Sim::Device {
public:
    struct MyChipPins {
        Sim::Bus D;
        Sim::Pin NotCS, CLK;
    };
    MyChipPins Pins;

    MyChip(Sim::Platform* p)
        : Pins{ Sim::Bus::New(p, 8, "MyChip.D"), Sim::Pin::New(p, "MyChip.NotCS"), Sim::Pin::New(p, "MyChip.CLK") }
    {
        Pins.NotCS.AddTrigger(this, Trig::Select);           // immediate: combinational response
        Pins.CLK.AddTrigger(this, Trig::Clock, false);       // stable: edge/latch once per cycle
    }

    // Not virtual, no `override`: Device doesn't declare Logic. AddTrigger's
    // first call captures MyChip's concrete type and registers a type-erased
    // thunk, so Platform dispatches here without a vtable. `final` documents
    // that Logic has no further override.
    void Logic(Sim::TriggerMask mask) {
        // Mask check first: this block only reacts to the Clock edge, so there
        // is nothing to read before confirming the edge fired.
        if (mask.IsSet(Trig::Clock)) { /* latch _value from settled inputs */ }

        // NotCS is a level, not an edge: Clock can fire while NotCS stays low
        // from an earlier call, so read its level on every call. Gating this
        // on mask.IsSet(Trig::Select) would leave D stale whenever Clock
        // updates _value without NotCS toggling in this call.
        if (Pins.NotCS.IsLow()) Pins.D.Write(_value);
        else Pins.D.Clear();                                  // release the bus (High-Z)
    }

private:
    enum class Trig : uint8_t { Select = 0, Clock = 1 };      // bit index, max 63
    uint8_t _value = 0;
};
```

(Adjust include relative path if file sit at different depth.)

Rules for new device or change:

- Device register with `Platform` on its **first** `AddTrigger`. Device with no trigger, never run.
- **Immediate** trigger run during settle loop, maybe several time per cycle; use for decode and combinational output. **Stable** trigger (`false`) run once after settle; use for clock edge and write strobe.
- `TriggerMask` give `IsSet(bit)` (one trigger; pass device own trigger enum, not raw `uint8_t`), `IsSetAny(bits...)` (OR), `IsSetAll(bits...)` (AND).
- Put mask check first, in any block of `Logic()` that exist only to react on specific trigger (latch data on clock edge, emit debug line once per write). Cheaper than pin read, and `Logic()` sit in hot loop. **Do not** use mask check in place of level or enable check. Select, chip-enable or reset line *current* state can matter on call trigger by different pin, so read that pin direct and unconditional, like skeleton do with `Pins.NotCS.IsLow()`, like `Register8.hpp` do with `Pins.NotCE.IsLow()`. See `ST16C550.md` and `W65C51N.md`, same reasoning about `RESET`/`RESB`.
- Read inside `Logic()` see net frozen at batch start. Cannot read back own write in same call.
- Write stateful. Always release shared bus (`Bus::Clear()` / `Sim::LogicState::F`) when not drive. Two strong driver give `E`, `DeviceHarness` flag this after every `step`, so watch for `CONTENTION` line, not only wrong-look value.
- Avoid zero-delay feedback through immediate trigger; settle loop never end then.
- Gate debug output on `Diagnostics::debugMode`, pass `Sim::Diagnostics&` like `ParallelSram` and `W65C02` do, log transition only.
- Anything touch sim from other thread (socket, timer, keyboard) must go through `Platform::Enqueue()`. See `TcpTerminal` (`src/Devices/Serial/`) for usual pattern. `ST16C550.md`'s "Threading and locking" write down one narrow, deliberate exception; read it before treat that exception like precedent.
- Constructor param beyond pin/diagnostics pair (mode enum, frequency, byte count) pass through script `device ... key=value` token via `ArgsMap`/`ArgValue` (`AsInt()`, `AsString()`, `LoadBytes()` for leading `@path`; see `HarnessCore.hpp` and `device-harness`). Design constructor with this in mind, not bolt on later at step 5.

**Extending:** new constructor param break every harness registration and `.devtest` already there, unless it got default, or you update shim and all script same change. Prefer new behavior reachable through pin already there.

## 4. Document it

Every device get short `<Name>.md` next to code. Use this outline, only section that apply:

1. **Introduction:** one, two sentence on what chip is and do.
2. **Pinout reference:** every pin and bus, one line each.
3. **Internal state reference:** every register, constant, bit field, mode value, as table reader look thing up in.
4. **Behavior:** trigger, timing, access model, anything not guess from reference above.
5. **Added features:** anything beyond literal register model (e.g. construction-time mode switch).
6. **Limitations:** deviation from datasheet and why, anything simplified.
7. **Test coverage:** what `.devtest` exercise, what it deliberate not.

Trivial device doc go from introduction straight to behavior. Write section only when it got content, never placeholder, match length to device, not to other device doc.

Doc convention in `AGENTS.md` apply: concise, standalone (no compare to other device or machine unless cannot avoid), current behavior only. `src/Devices/Io/W65C22/W65C22.md` worked example for big register surface; one-register device doc just few line.

**Extending:** update `<Name>.md` already there, in place, add new register, mode or pin to section it belong, like always there. No append "recently added" note.

## 5. Verify with DeviceHarness

Register device with `DeviceHarness` (see `device-harness` skill), write focused `.devtest` script, drive its pin. This verify step for device.

- For multi-register or multi-mode chip, verify each register and mode in own focused section (or script), like step 1 incremental build-out intend. Note in device `.md` which register, mode, boundary case got exercise, which not.
- Watch for `CONTENTION` line after any `step`. Mean script `drive` and device output fight over pin, almost always missing `clear`.
- Device that advance state from background thread, check with `sleep <milliseconds>` (see `device-harness` for drain-timing nuance). Keep such check ad hoc, not hard-code in committed `.devtest`. For what `sleep` cannot reach, see `DeviceHarness.md`'s "Verifying real-time or background-thread behavior".
- Commit passing script as `<Name>.devtest`, standing regression test.

**Extending:** add focused section for new behavior to `.devtest` already there, then re-run whole script, confirm nothing regress.

## Debugging a misbehaving device

Reproduce, isolate, fix, verify:

1. State failing observable precise (expect vs actual register or pin value, select/strobe timing, or contention), confirm it reproduce at register/pin level alone. If only reproduce through real CPU and `Chipset`, that machine concern (see last section).
2. Reproduce with smallest `.devtest` that drive suspect pin, or smallest external stimulus for host-side I/O like serial byte. Register device first if need.
3. Read device `<Name>.md`. Its "Limitations" and "Threading and locking" section list known simplification and one legit host-thread write. Do not mistake documented simplification for bug, treat undocumented host-thread write elsewhere as suspect race, not precedent.
4. Read harness output: device edge-detect strobe instead of re-fire on every immediate re-evaluation? Read and write right bit? Release bus when not select? Watch for `CONTENTION`.
5. Compare with datasheet (or text description) for that register or mode: trigger level, self-clear vs sticky bit, read-only/write-only/reserved register. Check read side, write side, independent.
6. Make smallest fix in one owning file. Never compensate for device bug in `Chipset` or other device.
7. Re-run reproducer plus one adjacent boundary case (adjacent address, mode or trigger level, or FIFO occupancy), fold reproducer into committed `.devtest`, update `.md` if documented behavior change.

For long session, or when not clear if bug in this device or in CPU/bus sequencing, hand off to `.claude/agents/peripheral-device-debugger.md` (device/register level) or `.claude/agents/mos-stepper-debugger.md` (CPU/bus).

## Machines are a separate concern

Device build, change or fix this way, complete on own. Combine device into working computer (address decode, real CPU/bus, firmware) belong to `Machine` (e.g. `src/Machines/BenEater6502/`) and `machine-engineer` skill. Wire device into machine only when task explicit ask for that, as separate activity.