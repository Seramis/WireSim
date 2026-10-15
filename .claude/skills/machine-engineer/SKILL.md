---
name: machine-engineer
description: Compose a new Machine (CPU + memory + peripherals + Chipset glue + firmware) from existing, already-verified Sim::Device components, or wire a new peripheral into an existing machine, verified with MachineHarness. Use when building or extending a running computer out of devices that already pass their own DeviceHarness tests -- never for designing or modifying a device itself.
---
# Composing a machine from devices

This skill combine already-built `Sim::Device`s into running `Machine`: address decode across chips, real CPU and bus, firmware image. `device-cpp` and `device-verilog` declare out of scope, `DeviceHarness` cannot verify this (see `DeviceHarness.md`, "Why single-device"). `src/Machines/BenEater6502/` worked example; files = skeleton for every step below.

Skill **never create, edit or retrofit device**. Every component must already exist under `src/Devices/` and pass own `.devtest`. Missing device built first with `device-cpp`/`device-verilog`. Device exist but not inspectable (see "Inspection check") retrofitted, if at all, with `device-inspect`.

## Which case is this?

- **New machine:** follow every phase in order.
- **Extending a machine:** wire one new peripheral into existing machine. Most of "Place" already done. Focus on `Chipset` decode change, machine's `AGENTS.md` memory map, new `.machinetest` section for peripheral's address range.
- **Debugging a wiring-only symptom:** `.machinetest` assertion fails while every component's own `.devtest` passes, decoded address ranges overlap, or firmware memory map disagree with what `Chipset` decodes. Hand long session to `.claude/agents/machine-wiring-debugger.md`.

## Required inputs

State these up front or ask for them. Skill never invent memory map, component list or clock topology.

1. **Component list:** CPU, memory, every peripheral, each existing verified `Sim::Device`. If serial console I/O needed and full UART not, `src/Devices/Serial/BusTerminal/` deliberately minimal CE/OE/WE/D-only device giving TCP console without UART's register model.
2. **Memory/IO map:** address ranges per component, read/write strobe semantics, interrupt wiring. Accept as prose or table in task, referenced markdown file, or "model after `src/Machines/BenEater6502/AGENTS.md`'s **Machine facts**".
3. **Clock/reset topology:** single shared clock (as in BenEater6502) or something else, which components reset fans out to.
4. **Firmware toolchain** for CPU: reuse existing one (ca65/ld65 for another 6502 machine) or specify new one.

If any input incomplete or ambiguous, say so, ask instead of guessing memory map or reset duration.

## Inspection check

Before wiring, check whether CPU and main memory device implement `Sim::IRegisterInspectable` / `Sim::IMemoryInspectable` (`src/Simulator/Inspect.hpp`). Grep header for base classes or check device's `.md`.

If either missing, **stop, ask user** whether to run `device-inspect` on it first (enables register and memory `expect`s for this machine) or proceed without state assertions for that component. Skill never edits device itself.

## Phase 1: Place

Create `src/Machines/<Machine>/` with:

- **`<Machine>.hpp`:** `Sim::Platform&` member, shared `Pin`/`Bus` members for clock, reset, address, data, one member per chip (CPU, memory, every peripheral) constructed directly, not through `unique_ptr` or factory. Add any `Terminal`-derived host bridge (`TcpTerminal`) for serial console. Constructor build pins/buses, construct every chip, call private `Setup()`.
- **`Setup()`:** pull down any floating bus or pin needing defined idle level (`Pin::PullDown`/`PullUp`), then call one `SetupX()` per chip that `Platform::Link`s chip's pins to shared bus/pins, or to `Chipset`'s decoded enables (see `BenEater6502.hpp`'s `SetupSram()`). All `Link`/`AddTrigger` calls happen before `Platform::Start()`.
- **`Chipset.hpp`:** plain `Sim::Device` (same `AddTrigger` / `Logic(TriggerMask)` pattern as any device) owning every peripheral's `/CE`, `/WE`, `/OE` (or chip-specific strobe) pins. Combinationally decode every address range into those enables, using named address-range constants, treating "everything else" as memory or unmapped. Use separate handlers, each own trigger bit, for anything else such as interrupt OR-ing or reset fan-out (see `src/Machines/My6502/Chipset.hpp`).
- **`Firmware/`** and **`scripts/build-firmware.sh`:** see Phase 2.
- **`machine.cmake`:** registers machine with build. One `wiresim_add_machine()` call (executable name, header, default firmware variant, `.machinetest`), one `wiresim_add_firmware()` per ROM variant, including `machinetest` variant for Phase 4. See `BenEater6502/machine.cmake`. Never edit `CMakeLists.txt` or IDE config for new machine.
- **`AGENTS.md`:** see Phase 3.

To be `MachineHarness`-eligible, `<Machine>.hpp` also needs:

- Accessors (or public members) expose CPU and memory as `IRegisterInspectable&` / `IMemoryInspectable&`.
- `LoadRom(romDir)` taking directory instead of hard-coded bank paths.
- `TickHalfCycle()` / `HoldReset(halfCycles)` pair factored out of machine's interactive `Start()` loop (see `BenEater6502::Start`'s per-half-cycle body), callable from `MachineHarness`'s run loop.

Design these in from start; shim in Phase 4 needs them.

## Phase 2: Firmware build contract

Firmware build has fixed toolchain-agnostic outer layer, toolchain-specific inner layer, so different CPU needs only new inner script. `src/Machines/BenEater6502/scripts/build-firmware.sh` and `Firmware/build.sh` worked example. Copy outer script's structure (argument parsing, temp-dir isolation), change only inner script's toolchain calls.

- **`scripts/build-firmware.sh [-o DIR] [-r DIR] [<toolchain-mode-flags>]`:** copy sources to isolated work directory (never touch tracked copy), run `Firmware/build.sh <work-firmware-dir> <rom-out-dir> [obj-out-dir]`, print resulting ROM directory, exit with that script's status. Machine whose firmware has scratch entry file (My6502's `Firmware/debug.asm`) can also accept scratch file overwriting only work copy; name that file in machine's `AGENTS.md`.
- **`Firmware/build.sh <firmware-dir> <rom-out-dir> [obj-out-dir]`:** specific to machine's CPU and toolchain. Takes two required positional arguments plus optional third for assembler listings, write whatever binary image format machine's memory device loads into `<rom-out-dir>`. Toolchain knobs not universal pass through environment, keeping required arguments generic.

`-r DIR` writes ROM images somewhere other than `<work dir>/rom/<Machine>`; CMake uses it to land them in `build/<preset>/rom/<Machine>-<variant>`. `MachineHarness`'s `rom=<DIR>` and manual `build-firmware.sh -o DIR` point at same kind of directory, whatever toolchain produced it.

## Phase 3: Document

Write `<Machine>/AGENTS.md` in shape of `src/Machines/BenEater6502/AGENTS.md`: build and run (the `build-firmware.sh` invocation), how to run machine, debug mode, any scratch entry file, machine facts (memory map, clock, reset duration, trace tags). Describe current behavior only.

## Phase 4: Verify with MachineHarness

Add `<Machine>.machine-harness.cpp` shim (template in `src/Testing/MachineHarness.md`, "Registering a new machine") exposing Phase 1 accessors, then write `.machinetest` script covering:

- Boot reaches expected reset vector or entry point (an `expect <cpu>.PC = ...` after enough `run` cycles to clear reset and first fetches, if CPU inspectable).
- One representative read and write through each peripheral's decoded address range. Proves `Chipset`'s decode, **not** peripheral's own correctness, already covered by `DeviceHarness`.

Commit as `<Machine>.machinetest` with `rom=rom/<Machine>-machinetest` (ctest runs it from build directory), registered with `TEST` in `machine.cmake`. Standing regression test, like `.devtest`.

If CPU or memory not inspectable (declined at "Inspection check"), script can verify only what needs no register or memory `expect`: that boot does not crash, or `DebugWriter`-style device's log output. Say so in machine's `AGENTS.md` instead of silently having no regression test.

## Scope

Skill verifies multi-device wiring, address decode, firmware boot/run behavior, using only devices already passing own `.devtest`. Never creates, edits or retrofits device. New devices come from `device-cpp`/`device-verilog`, inspectability from `device-inspect`, always as separate prior work skill asks user about instead of doing itself.