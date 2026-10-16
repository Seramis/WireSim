---
name: device
description: Add, extend, modify, audit or debug any hardware device in the WireSim netlist (C++ or Verilog chip, memory, peripheral, glue logic, CPU), verified standalone with DeviceHarness. Also covers exposing register/memory state (IRegisterInspectable/IMemoryInspectable). Use when creating a Sim::Device, adding pins/registers/triggers/instructions, retrofitting inspection, implementing or auditing a CPU instruction set, or fixing a device that misbehaves at the register/pin level.
---
# Devices in WireSim

Skill covers life of `Sim::Device`: add, extend, audit, fix. CPU is ordinary device; differs only in test script style (script is the memory, see `device-harness` → `bus-master.md`). All work through `DeviceHarness`: no CPU, no ROM, no machine. Wiring devices into machine is separate activity (last section).

Read `overview.md` first for trigger and phase meaning. `src/Devices/Memory/Sram/ParallelSram/ParallelSram.hpp` is small complete example. `src/Devices/Io/W65C22/W65C22.md` shows doc of big device.

Reference files next to this one, read only when they apply:

- [verilog.md](verilog.md): device written in Verilog (`.v` + `.f` manifest, verilated, wrapped as `Sim::Device`).
- [coverage.md](coverage.md): device has large register, mode or instruction surface, or task is audit or completeness claim. Method: independent oracle, coverage file, test ordering, generated vectors.
- [cpu.md](cpu.md): device is CPU. ISA-specific inventory, ordering, special areas, on top of `coverage.md`.

## Which case is this?

- **New device:** chip not yet in `src/Devices/`. Follow steps 1-6.
- **Extending or modifying:** new register, mode, pin or behavior on chip already there. Follow steps, use each step's "Extending" note, skip step 2.
- **Retrofitting inspection:** device exists, only needs state exposed. Go to step 4.
- **Auditing:** check existing device against spec. Read `coverage.md`.
- **Debugging:** wrong value, timing or contention. Go to "Debugging a misbehaving device".

## 1. Understand the chip (or the change)

Spec is datasheet PDF or text description. Pull same facts either way before writing code.

- **Datasheet PDF** (`docs/<chip>.pdf`): run `pdftotext -layout docs/<chip>.pdf -`. Without `-layout`, tables scramble. Read table of contents first, then function description and register/mode tables, holding nearly all behavior model needs. Skip DC/AC characteristics, package pinouts, ordering info.
- **Text description:** pull register/pin map, modes, timing from sentences. Sentence doesn't pin down behavior (what reserved bit reads back, what happens on simultaneous read and write): say so in device `.md`, don't guess silently.

**Extending:** read device's `<Name>.md` first, especially "Internal state reference" and "Limitations", so change fits existing design.

Decide and write down before coding:

- Which registers are read/write-asymmetric (write-only, read-only, context-dependent like DLAB, reserved on write), which bits stateful, self-clearing or sticky.
- Whether chip needs bit- or cycle-accurate timing, or can be modeled at transaction/register level. `ST16C550.md`'s "Byte-level, not bit-level" shows one such tradeoff.
- Whether anything drives device from outside sim thread (byte stream, timer, keyboard). If so, route through `Platform::Enqueue()` / `EnqueueWithFuture()` (step 3), never direct write from host thread.
- What you deliberately don't model. Every device here writes this down, so decide list up front.
- Order of load and count/shift on same clock edge. Most datasheets put them on different edges. Get it wrong and load eats its own first count. Also check whether accessing register that re-arms free-running process could let process tick before your check reads result.

Chip with big register or mode surface (more than handful of registers, or several modes; 16C550-class UART or bigger) not built in one pass. Build register-map skeleton first, every register wired, round-trip readable and writable, check with focused `.devtest` section. Then add one mode or feature at a time, check each. Keep each diff small so it can be bisected. Tracking what is verified: see `coverage.md`.

## 2. Place the code (new device only)

- Put machine-agnostic chip in `src/Devices/<Category>/<Name>/<Name>.hpp`, plus `.cpp` if needed. Every device gets own folder, soon holding `.md`, `.harness.cpp`, `.devtest` too (categories in `AGENTS.md` repository map).
- New `.cpp` picked up by `CMakeLists.txt` glob after re-configure: `cmake --preset debug`.

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

(Adjust include path if file sits at different depth.)

Rules for new device or change:

- Device registers with `Platform` on **first** `AddTrigger`. Device with no trigger never runs.
- **Immediate** trigger runs during settle loop, possibly several times per cycle; use for decode and combinational output. **Stable** trigger (`false`) runs once after settling; use for clock edges and write strobes.
- `TriggerMask` offers `IsSet(bit)` (one trigger; pass device's own trigger enum, not raw `uint8_t`), `IsSetAny(bits...)` (OR), `IsSetAll(bits...)` (AND).
- Put mask check first in any `Logic()` block existing only to react to specific triggers (latch data on clock edge, emit debug line once per write). Cheaper than pin read, and `Logic()` sits in hot loop. **Don't** use mask check in place of level or enable check. *Current* state of select, chip-enable or reset line can matter on call triggered by different pin, so read that pin directly and unconditionally, as skeleton does with `Pins.NotCS.IsLow()` and `Register8.hpp` does with `Pins.NotCE.IsLow()`. `ST16C550.md` and `W65C51N.md` make same point about `RESET`/`RESB`.
- Reads inside `Logic()` see nets frozen at batch start. Device can't read back own write in same call.
- Writes are stateful. Always release shared bus (`Bus::Clear()` / `Sim::LogicState::F`) when not driving. Two strong drivers give `E`, and `DeviceHarness` flags it after every `step`: watch for `CONTENTION` line, not only wrong-looking values.
- Avoid zero-delay feedback through immediate triggers; settle loop never ends then.
- Gate debug output on `Diagnostics::debugMode`, passing `Sim::Diagnostics&` as `ParallelSram` and `W65C02` do, log transitions only.
- Anything touching sim from another thread (socket, timer, keyboard) must go through `Platform::Enqueue()`. See `TcpTerminal` (`src/Devices/Serial/`) for usual pattern. `ST16C550.md`'s "Threading and locking" records one narrow, deliberate exception; read it before treating that exception as precedent.
- Constructor parameters beyond pin/diagnostics pair (mode enum, frequency, byte count) pass through script's `device ... key=value` tokens via `ArgsMap`/`ArgValue` (`AsInt()`, `AsString()`, `LoadBytes()` for leading `@path`; see `HarnessCore.hpp` and `device-harness`). Design constructor with this in mind, not as step-6 afterthought.

**Extending:** new constructor parameter breaks every harness registration and `.devtest` already there unless it has default or you update shim and all scripts in same change. Prefer new behavior reachable through pins already there.

## 4. Expose state (inspection)

Two opt-in interfaces in `src/Simulator/Inspect.hpp` let tooling read device after sim stops. Never called from `Logic()` or other hot path, so ordinary virtual dispatch fine even though `Device::Logic` avoids vtable.

- `Sim::IRegisterInspectable`: register-holding devices. `DumpRegisters()` returns `vector<pair<string, uint64_t>>` of every register worth showing. **Required for CPU**, and any device whose internal state tests should assert.
- `Sim::IMemoryInspectable`: byte-addressable storage (RAM, ROM). `InspectableSize()`, `Peek(address)`, `Poke(address, value)`.

Most devices need neither. Add when test needs internal state, or when machine's `.machinetest` will assert on device. Add-only change: base class and one or two `const` methods; pins, registers, behavior unchanged. Never add as side effect of building machine; `machine-engineer` stops and asks user first.

```cpp
class MyChip final : public Sim::Device, public Sim::IRegisterInspectable {
public:
    // ... existing Pins, constructor, Logic() unchanged ...

    std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
        return {{"A", _a}, {"X", _x}, {"Y", _y}, {"PC", _pc}};
    }
};
```

`Peek`/`Poke` read and write device's existing backing storage directly. Skip pins and `Logic()`, so safe any time, even before `Platform::Start()` (how `MachineHarness`'s `Boot()` and `poke` use them).

In harness shim, set matching capability pointer next to pins (skip line that doesn't apply; both default null):

```cpp
h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
h.Memory = dynamic_cast<Sim::IMemoryInspectable*>(dev.get());
```

Then exercise interface in `.devtest`. `dumpregs`, `peek`, `poke`, `expect reg` and `expect mem` fail at parse time against device whose matching pointer is null.

```
device MyChip
dumpregs
expect reg.A = 0x00
poke 0x10 0x7F
expect mem[0x10] = 0x7F
```

Checking many registers or addresses: define macro (`def EXPECT_REGS` with one `expect reg.X = $N` per register; see `device-harness`) so each check point is one call. Document interface in device `.md`, in section covering its state (usually "Internal state reference"); it's existing state becoming externally visible, needs no section of its own.

## 5. Document it

Every device gets short `<Name>.md` next to code. Use this outline, only sections that apply:

1. **Introduction:** one or two sentences on what chip is and does.
2. **Pinout reference:** every pin and bus, one line each.
3. **Internal state reference:** every register, constant, bit field, mode value, as table reader looks things up in.
4. **Behavior:** triggers, timing, access model, anything not guessable from reference above. Bus master adds its **Bus profile** here (see `device-harness` → `bus-master.md`).
5. **Added features:** anything beyond literal register model (e.g. construction-time mode switch).
6. **Limitations:** deviations from datasheet and why, anything simplified.
7. **Test coverage:** what `.devtest` exercises, what it deliberately doesn't.

Trivial device doc goes from introduction straight to behavior. Write section only when it has content, never placeholder, match length to device, not to other device docs.

`AGENTS.md` doc conventions apply: concise, standalone (no comparison to other devices or machines unless unavoidable), current behavior only. `src/Devices/Io/W65C22/W65C22.md` is worked example for big register surface; one-register device doc is just few lines.

**Extending:** update existing `<Name>.md` in place, put new register, mode or pin in section where it belongs. No "recently added" note.

## 6. Verify with DeviceHarness

Register device with `DeviceHarness` (see `device-harness`), write focused `.devtest` script, drive its pins. This is verification step for device.

- Multi-register or multi-mode chip: verify each register and mode in own focused section (or script), as step 1's incremental build-out intends. Note in device `.md` which registers, modes, boundary cases exercised and which not.
- Define macros (`def <name> ... end`, positional `$0`, `$1`... arguments; see `device-harness`) for sequences script repeats, such as `WRITE_REG addr value` and `READ_REG addr`, so each section reads as list of operations and boundary case is one more call.
- Watch for `CONTENTION` line after any `step`. Means script's `drive` and device's output fight over pin, almost always missing `clear`.
- Device advancing state from background thread: check with `sleep <milliseconds>` (see `device-harness` for drain-timing nuance). Keep such checks ad hoc, not hard-coded in committed `.devtest`. What `sleep` can't reach: `DeviceHarness.md`'s "Verifying real-time or background-thread behavior".
- Device driving bus itself (CPU, DMA) tested with bus-cycle macros; see `device-harness` → `bus-master.md`.
- Commit passing script as `<Name>.devtest`, standing regression test.

**Extending:** add focused section for new behavior to existing `.devtest`, then re-run whole script to confirm no regression.

## Debugging a misbehaving device

Reproduce, isolate, fix, verify:

1. State failing observable precisely (expected vs actual register or pin value, select/strobe timing, or contention), confirm it reproduces at register/pin level alone. Reproduces only through real CPU and `Chipset`: machine concern (see last section).
2. Reproduce with smallest `.devtest` driving suspect pin, or smallest external stimulus for host-side I/O such as serial byte. Register device first if needed.
3. Read device's `<Name>.md`. Its "Limitations" and "Threading and locking" sections list known simplifications and the one legitimate host-thread write. Don't mistake documented simplification for bug; treat undocumented host-thread write elsewhere as suspect race, not precedent.
4. Read harness output (script long and repetitive: first fold repeated lines into macro so suspect sequence readable). Does device edge-detect strobe instead of re-firing on every immediate re-evaluation? Read and write right bits? Release bus when not selected? Watch for `CONTENTION`. Bug shows only under host-thread input, not direct register accesses: suspect missing `Platform::Enqueue()`, mutex gap, or race between sim thread and worker thread.
5. Compare with datasheet (or text description) for that register or mode: trigger level, self-clearing vs sticky bit, read-only/write-only/reserved register, interrupt-priority encoding. Check read side and write side independently; chip can get one direction right, other wrong.
6. Make smallest fix in the one owning file. Never compensate for device bug in `Chipset` or another device. Fix touches helper shared by several registers, modes or instructions: check every call site against what it implements.
7. Re-run reproducer plus one adjacent boundary case (adjacent address, mode, trigger level or FIFO occupancy), fold reproducer into committed `.devtest`, update `.md` if documented behavior changed.

Don't infer timing from neighbouring output. Correlate by step number and elapsed time (`[s=N t=M]`), pin values driven and expected at that step, device's own select and strobe pins. `t=` (real ms since run phase began) diverges from cycle count only near `sleep` or device's own background thread; that's where real-time race shows.

Long session: delegate to `.claude/agents/device-debugger.md`. Symptom needs real `Chipset` or firmware to appear: belongs to `.claude/agents/machine-wiring-debugger.md`.

## Machines are a separate concern

Device built, changed or fixed this way is complete on its own. Combining devices into working computer (address decode, real CPU/bus, firmware) belongs to `Machine` (e.g. `src/Machines/BenEater6502/`) and `machine-engineer` skill. Wire device into machine only when task explicitly asks, as separate activity.
