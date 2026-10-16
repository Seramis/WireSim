# AGENTS.md: WireSim

> **Scope:** this repo only. Stack: C++23, CMake, ca65, Verilator.

## What this is

WireSim: netlist-based, event-driven digital logic simulator, cycle-accurate down to half clock cycles. Three slices:

- **`Simulator`**: generic netlist and event engine.
- **`Devices`**: reusable, machine-agnostic hardware (CPUs, memory, peripherals), each doc'd next to its code.
- **`Machines`**: concrete computers built from devices, glue logic and firmware.

Two machines:

- **BenEater6502** (default one): Ben Eater's breadboard 6502 computer, runs Microsoft BASIC from vendored `msbasic` submodule. Has W65C22 VIA, W65C51N ACIA bridged to TCP, HD44780 LCD.
- **My6502**: breadboard-style 6502 computer, 16C550 UART, 64 KiB SRAM, debug peripherals. Runs 6502 assembly (ca65/ld65) assembled to ROM images. Also home of assembly test workflow (`asm-test`, `sim-run.sh`).

Performance: main goal here. Commit subjects show measured half-cycle rate (e.g. "3.94MHz"). Treat `src/Simulator/` and `Bus`/`Pin` paths as hot code.

## Documentation map

- [overview.md](overview.md): simulator core (pins, nets, logic states, immediate and stable triggers).
- [src/Devices/Cpu/W65C02/W65C02.md](src/Devices/Cpu/W65C02/W65C02.md): W65C02 CPU model, bus profile and test coverage.
- [src/Devices/Cpu/Z80/Z80.md](src/Devices/Cpu/Z80/Z80.md): Z80 CPU model, bus profile, limitations, test coverage; [Z80.coverage.md](src/Devices/Cpu/Z80/Z80.coverage.md) lists every instruction form w/ encodings, timing, tests.
- [src/Testing/DeviceHarness.md](src/Testing/DeviceHarness.md): `DeviceHarness`, exercises one device thru its pins, no CPU, ROM or machine needed. Expected and sufficient verification for a device. Scripts (here and in `MachineHarness`) support `def <name> ... end` macros with positional `$0`, `$1`... arguments: factor any repeated sequence (register access, bus cycle, reset, check group) into one.
- [src/Testing/MachineHarness.md](src/Testing/MachineHarness.md): `MachineHarness`, boots machine from pre-built ROM, runs it headless, asserts CPU/memory state.
- [docs/](docs/): datasheets for devices built in this repo.
- Per-component `.md` files sit next to code (see "Repository map"). Check component's dir for one before trusting comments or guessing from source.

Skills in `.claude/skills/`:

| Skill | Use it to |
| --- | --- |
| `device` | add, extend, audit or debug any device (C++ or Verilog, peripheral or CPU), incl. opt-in register/memory inspection, verified w/ `DeviceHarness` |
| `device-harness` | exercise one device's pins w/ `.devtest` script; bus masters (CPU) driven w/ bus-cycle macros |
| `machine-engineer` | compose verified devices into `Machine`, verified w/ `MachineHarness` |
| `asm-test` | write and run focused 65C02 test program |

## Repository map

| Path | Contents |
| --- | --- |
| `src/Simulator/` | Engine, namespace `Sim`: `Platform` (nets, bake, run loop), `Pin`, `Bus` (`Bus::Read<T>` lives in `Platform.hpp`, needs `Platform`'s complete type), `LogicState`, `Device` (`Device`/`TriggerMask`), `Diagnostics` (cycle counters, debug flag). `Sim::detail` (`DirtyTagSet`, `LogicState` LUTs) just impl detail, not part of device/machine API. |
| `src/Devices/<Category>/<Name>/` | Reusable hardware, one folder per device however many files it needs (e.g. `Memory/Sram/ParallelSram/`, `Serial/ST16C550/`, `Cpu/W65C02/`). Device folder can also hold its `.md` doc, `.harness.cpp` registration and `.devtest` script. Current categories: `Cpu`, `Debug`, `Graphics`, `Io`, `Memory`, `Panel`, `Register`, `Serial`. Add new category only when no existing one fits, name it for device's electrical role in one generic word (e.g. `Io`), not a product or protocol family. Category folder can also hold shared non-device code at its own level (e.g. `Serial/Terminal.hpp`). |
| `src/Support/` | Cross-cutting helpers that ain't a `Sim::Device` and fit no single category (e.g. `SdlEventHub`, `SdlUiTestHooks`). Flat `<Name>.hpp(+.cpp)` plus colocated `.md`. |
| `src/Testing/` | `DeviceHarness` and `MachineHarness`, separate CMake targets (not the machine binaries). Device opts in with colocated `<Name>.harness.cpp` and `<Name>.devtest`; machine with `<Machine>.machine-harness.cpp` and `<Machine>.machinetest`. Both shims excluded from machine builds. |
| `src/Machines/<Machine>/` | `<Machine>.hpp` (wiring, ROM loading, run loop), `Chipset.hpp` (address decode), `machine.cmake`, `Firmware/`, `scripts/` and an `AGENTS.md` w/ machine facts. See [BenEater6502](src/Machines/BenEater6502/AGENTS.md) and [My6502](src/Machines/My6502/AGENTS.md). |
| `src/Main.cpp`, `CMakePresets.json` | `Main.cpp` is the one CLI entry point (`--cycles N`, `--variant NAME`, `--rom DIR`), compiled once per machine by `wiresim_add_machine()` cuz machine headers can't share a translation unit (`W65C02.hpp` has no include guard). `CMakePresets.json` defines `debug` and `release` presets (Ninja, clang, `build/<preset>`). |
| `build/<preset>/` | Generated and git-ignored. Holds executables and each machine's assembled firmware under `rom/<Machine>-<variant>/`. |

## Build & run

Toolchain: `clang++` (C++23), CMake ≥ 3.20, Ninja or Make, `verilator` (verilates every device under `src/Devices/**/*.f`), `ca65`/`ld65` (cc65), SDL2 dev headers and libs (for devices w/ SDL window; Fedora: `sdl2-compat-devel`) and optionally `socat` for `connect.sh`.

Everything builds thru CMake presets, firmware included:

```bash
cmake --preset debug                            # once, and after adding files
cmake --build --preset debug                    # executables, harnesses, all firmware variants
cmake --build --preset debug --target check     # build, then ctest: every .devtest and .machinetest
build/debug/BenEater6502 --cycles 1000      # BenEater6502
build/debug/WireSim --cycles 1000              # My6502
```

Each machine executable takes `--variant NAME` (firmware built to `<exe dir>/rom/<Machine>-NAME`) or `--rom DIR`, runs from any dir. In VS Code, CMake Tools reads the presets: pick preset and launch target in status bar, use its Debug button (gdb, breakpoints in C++).

**Running a machine yourself:** hold stdin open, always pass `--cycles`. Bare run exits immediately on stdin EOF or never ends. `src/Machines/My6502/scripts/sim-run.sh` does this for My6502; see its `AGENTS.md`.

**Adding a machine** needs no edit to `CMakeLists.txt`, presets or IDE config. Give `src/Machines/<Machine>/` a `machine.cmake` (see `BenEater6502/machine.cmake`) calling `wiresim_add_machine()` once and `wiresim_add_firmware()` per ROM variant. Machine-specific `scripts/build-firmware.sh` assembles each variant. Its `.machinetest` names its ROM as `rom/<Machine>-<variant>`, relative to build dir ctest runs from. Use the `machine-engineer` skill.

**Verification:** no unit-test suite for engine itself; `check` runs device and machine scripts only. Device verified w/ `DeviceHarness`: colocated `.devtest` script drives its pins directly. That's the expected and sufficient step for device work (`device` skill). **Agents not expected to build or boot full machine just to add or change a device.** Wiring several devices into running computer (address decode, real CPU/bus, firmware) is machine work, verified w/ `MachineHarness`.

## Simulator core contract

Details in [overview.md](overview.md).

- **Public API** is namespace `Sim`: `Platform`, `Pin`, `Bus`, `Device`, `TriggerMask`, `LogicState`, `Diagnostics`. Devices and machines live in global namespace, qualify engine types w/ `Sim::`. Don't reach into `Sim::detail`.
- **Bake, then run.** All `Pin::New`/`Bus::New`/`Platform::Link`/`AddTrigger` calls must happen before `Platform::Start()`, which bakes topology once.
- **Six logic states:** `H`, `L`, `Hw`, `Lw`, `F`, `E`. Strong beats weak, weak beats `F`, `H` with `L` gives `E`. **Nothing checks for `E` at runtime.** Contention stored silently, reads as neither high nor low, so look for it explicitly when debugging bus fights.
- **Two phases per cycle.** Immediate loop resolves nets, runs immediate-triggered devices til stable. Then stable-triggered devices run once. Writes resolve next cycle.
- **Stable writes lag one cycle.** Pin written only from stable-triggered branch ain't visible thru `Pin::Read()` until cycle after the one that changed it. Applies to every device: check such pin one cycle later, not on the edge that changed it.
- **`Logic()` is atomic.** Reads see net values frozen at batch start, so device never reads back its own write in same call.
- **External input** from other threads goes thru `Platform::Enqueue()` / `EnqueueWithFuture()`. Never touch nets directly from outside sim thread.
- Zero-delay combinational loop never settles, hangs immediate loop.
- **Decode lag expected, not a bug.** When device reacts directly to signal (e.g. memory's address bus) while `Chipset` decodes same signal into its enables, enable change takes effect one iteration later. Only stable-triggered devices read results, run after loop converges, so harmless. Real bug only if new immediate-triggered device reads bus mid-settle and trusts glitch value. This only arises behind real `Chipset`, so machine concern.

## Coding conventions

- C++23, header-heavy. Most devices are header-only `Sim::Device` subclasses. `CMakeLists.txt` globs `src/**/*.cpp`, so re-configure after adding a `.cpp`.
- Place devices under `src/Devices/<Category>/` as above. Machine-specific glue like `Chipset` lives directly under `src/Machines/<Machine>/`.
- **Device pattern:** `final` class deriving from `Sim::Device`, nested `Pins` struct built from `Sim::Pin::New`/`Sim::Bus::New`. Register triggers in constructor w/ `AddTrigger(this, Bit, isImmediate)`. Implement plain (non-virtual, no `override`) `void Logic(Sim::TriggerMask mask)` that branches on `mask.IsSet(bit)` (prefer device's own trigger enum over raw `uint8_t`), `mask.IsSetAny(bits...)` or `mask.IsSetAll(bits...)`. `Logic` intentionally not declared on `Device`: first `AddTrigger` call captures concrete type, registers type-erased thunk, so dispatch skips vtable. Class w/o `Logic(TriggerMask)` fails to compile at its `AddTrigger` call.
- **Mask checks:** put mask check first in any block that exists only to react to specific triggers (latch data on clock edge, emit debug line once per write). Cheaper than a pin read, and `Logic()` runs in hot loop. Don't use it to shortcut level check (CE, CS, RESET) that must reflect pin's current state every call; read pin directly, else call triggered by another pin while enable stays asserted gives stale output. See `Register8.hpp`'s unconditional `NotCE` check and `RESET`/`RESB` notes in `ST16C550.md` and `W65C51N.md`.
- Release buses when not driving (`Bus::Clear()` / `Sim::LogicState::F`). Use `Sim::LogicState::ActiveLow/ActiveHigh/TriLow/TriHigh` helpers for control lines.
- Debug prints go thru `std::println`, must gate on `_diagnostics.debugMode`. Log transitions, not every `Logic()` call.
- `.clang-format` (LLVM base, tabs, attached braces) exists but codebase ain't uniformly formatted. Match surrounding file, don't reformat whole files.
- Keep hot paths allocation-free and branch-light. Compare half-cycle rate on Release build before and after perf work.

### Documentation conventions

- Markdown not hard-wrapped (`.editorconfig` sets `max_line_length = off` for `*.md`): write each paragraph as one line.
- Keep docs concise, easy to read. Say what reader needs, skip what they don't, prefer short direct sentences. Don't omit needed info, don't pad.
- Device's code comments and `.md` describe that device standalone. Don't name or compare another device or specific machine unless that's only way to explain behavior (e.g. two devices sharing one external resource). Skills and other instructional docs may point to a device as worked example.
- Describe current behavior only. History and rationale for a change belong in commit message, not in comment or doc.
- Where example machine needed, use BenEater6502.

## Agent workflow rules

- Don't compensate for CPU bug in chipset or memory device, or reverse. Fix the component that owns the behavior.
- Never leave `Firmware/debug.asm`, `DEBUG_MODE` or temp tracing modified unless user asked for it. Use `src/Machines/My6502/scripts/sim-run.sh <file>` w/ scratch `.asm` file instead.
- Don't infer bus timing from adjacent log lines. Correlate by `h=` counter, address, RWB and enables.
- Report verification honestly. For device, give exact `.devtest` script (or inline commands) and pass/fail count. For machine-level work, give exact program, cycle count and observed final state.
- `MachineHarness` checks final state after bulk `run <N>`; never correlates behavior cycle by cycle (see `MachineHarness.md`). Use `.machinetest` for "does this stay correct" regression coverage, trace-based tools (`sim-run.sh`, `asm-test`, `cycle-delta.sh`) for "why is this wrong" root-cause work.
- Specialized debugging agents (Claude Code: `.claude/agents/`; Copilot: `.github/agents/`):
  - Device implementation, audit and register/pin-level bugs, CPUs included (any architecture): `device-debugger`, verified with `DeviceHarness` (`device`, `device-harness` skills).
  - Whole-machine wiring bugs (address decode, enable fan-out, reset/interrupt wiring across chips): `machine-wiring-debugger` (Claude Code only), reproduced first w/ a `.machinetest` script.