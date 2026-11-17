# Bus-master devices (CPU, DMA, ...)

Device driving address and control lines itself needs no special harness support. Script plays rest of system: drives clock, reset, interrupt, ready pins, puts data on data bus when device reads, expects address and control lines (and data bus when device writes). No memory model: **list of bus-cycle calls is the memory** and also assertion of exactly what device did each cycle. Read registers with `dumpregs` / `expect reg.<name>` (`Sim::IRegisterInspectable`, see `device` skill, step 4).

Read [SKILL.md](SKILL.md) first (shim, grammar, macros, contention, stable-write lag). Method for implementing or auditing whole CPU in `device` skill (`coverage.md`, `cpu.md`).

## 1. Prerequisites

- Harness shim `<Name>.harness.cpp` exposing every pin (clock, address bus, data bus, control, reset, interrupts, wait/ready) and `h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get())`.
- `DumpRegisters()` returns every architectural register (CPU: see `device` → `cpu.md`).
- `cmake --build --preset debug --target DeviceHarness`

## 2. Establish bus profile

Before any script, pin down how device talks, record under "Bus profile" in `<Name>.md`. Each point is question bus-cycle macros depend on:

1. **Cycle boundary.** Which clock edge starts new bus cycle, which pins change (address, read/write/mem/io strobes, status such as M1).
2. **Sampling.** On which edge, until when, must read data be on data bus? When is write data valid?
3. **Steps per phase.** Outputs usually written from stable-triggered branch, visible one cycle after edge. `step 2` per clock phase usual minimum; confirm by probing.
4. **Read to write turnaround.** Stub keeps driving `D` until cleared. Device starts driving `D` while stub still does: harness prints `CONTENTION` (advisory). Avoid: after edge, `step`, `clear D`, `step`, then expect. Drive data only when device released bus.
5. **Reset and interrupts.** How many edges reset must be held, what bus cycles follow, when interrupt/NMI lines sampled.
6. **Register timing.** Registers usually update when cycle latched, i.e. start of *next* cycle. State asserted after a row reflects only cycles before it.

Probe with throwaway stdin script (`DeviceHarness -v -`): hold reset, toggle clock, `dump` after each step, note when each pin changes. Don't guess from source; observe.

## 3. Write script

Define one macro per bus-cycle kind at top of `.devtest`, then write program as one call per bus cycle (`def`/`end`, `$0`, `$1`... see `DeviceHarness.md` "Macros"):

```
def R                       # $0 = address the CPU must present, $1 = data fed back
  drive CLK = L
  step 2
  expect A = $0
  expect RWB = H
  drive D = $1
  drive CLK = H
  step 2
end
def W                       # $0 = address, $1 = data the CPU must write
  drive CLK = L
  step
  clear D
  step
  expect A = $0
  expect RWB = L
  expect D = $1
  drive CLK = H
  step 2
end

device W65C02
drive RESB = L
...
R 0x0200 0xa9
R 0x0201 0x42
W 0x0300 0x42
expect reg.A = 0x42
```

Complete example: `src/Devices/Cpu/W65C02/W65C02_lda_sta.devtest`. Device with more bus-cycle kinds defines more macros (e.g. M1 fetch, I/O read, I/O write); macros may call earlier macros.

Rules:

- First calls are reset sequence; release reset with ordinary `drive` line at point device must see it. Every later call is real bus cycle *manual* says instruction performs, **including dummy and internal cycles that appear on bus**.
- Program bytes and operands are data of read calls. Memory program reads or writes is only what calls say: store checked by its write call, load by feeding value then asserting register.
- Put `expect reg.<name> = <v>` after call that makes result visible (see 2.6). Assert *every* register in dump, not only target, to catch collateral changes.
- One instruction (or tightly related sequence) per section, each opened by `print TAG=<name>`, e.g. `print TAG=ADC_carry_in`.
- Expected values come from manual, never from running device and copying result.
- No `poke`/`peek`: no memory device.
- Several scripts for one device: name `<Name>_<group>.devtest`, underscore, not dot. CTest names script by part of file name before first dot, so `<Name>.<group>.devtest` collides with `<Name>.devtest`. Each file repeats macro definitions it needs.
- Large suites: generate script from throwaway program (see `device` skill's `coverage.md`). Commit generated `.devtest`, not program.

## 4. Read failure

`FAIL A expected=0x0201 actual=0x0200` at `[s=N ...]`: map failing `expect` back to call that produced it. Error and report lines carry *call's* line number; `[s=N]` step counter orders them. Decide in this order:

1. *Script* wrong? Re-read manual for that instruction's cycles. Wrong expectation most common cause.
2. *Macro* wrong (phase order, step count, missing `clear`)? Failure in very first calls of every script, or `CONTENTION`, points here.
3. Otherwise device bug. `DeviceHarness -v` prints device's own debug lines; `dump`/`dumpregs` mid-script show state; shorten script to smallest failing sequence.

Tell "extra or missing cycle" (first failing call is first one after divergence, every later call also fails) from "wrong value" (only that call and later register checks fail).

## 5. Commit

Commit `.devtest` files; CTest picks up every `*.devtest` under `src/Devices` (`cmake --build --preset debug --target check`). After editing script, re-run every `.devtest` in device's folder.
