# MachineHarness

Machine-level counterpart of [DeviceHarness](DeviceHarness.md). Boots one registered `Machine` from pre-built ROM directory, runs headless from small text script, asserts CPU + memory state instead of driving pins. See `.claude/skills/machine-engineer/SKILL.md` for where it fits when composing machine.

## Build & run

```bash
cmake --preset debug               # only needed after adding a new .machine-harness.cpp
cmake --build --preset debug --target MachineHarness
build/debug/MachineHarness [-v] <script.machinetest>
build/debug/MachineHarness [-v] -           # read the script from stdin
build/debug/MachineHarness --list-machines  # registered machine type names
```

Exit codes + `-v` match `DeviceHarness`: 0 if every `expect` passed, 1 if any failed, 2 for malformed script or CLI usage error; `-v`/`--diag` turns on `Sim::Diagnostics::debugMode`.

Tool does **not** build firmware. Script's `rom=<DIR>` points at ROM directory already built by machine's own `scripts/build-firmware.sh`. Machine's `machine.cmake` registers that build as firmware variant + script with CTest (`rom=rom/<Machine>-machinetest`, relative to build directory), so `cmake --build --preset debug --target check` builds ROM then runs script.

## Design

### Why there is no `drive`/`clear`

`DeviceHarness` drives pins because device's own pin behavior *is* what it tests. Machine's wiring can't be tested that way: only way to see whether `Chipset`'s decode right, or whether firmware boots, is run real firmware on real CPU + inspect result. So no pin-driving verb. State comes only from what `run <cpuCycles>` of real execution produces, read back through two opt-in interfaces (`Sim::IRegisterInspectable`, `Sim::IMemoryInspectable`; `src/Simulator/Inspect.hpp`) that machine's CPU + memory implement. Adding them to device is `device` skill's job (step 4); `MachineHarness` only reads them.

Machine whose components implement neither interface can still be registered, booted + run. An `expect`/`dump`/`poke` against point without matching interface fails at parse time, but rest of script still exercises boot + timing.

### Why one continuous `Platform::Start()`

`Platform::Start()` calls `Bake()` every time, + `Bake()` resets every net's drive state to floating (`Sim::LogicState::F`). Second `Start()` on same `Platform` (say, one per `run <N>`) would wipe clock, reset + bus pin states between calls + break running machine's continuity. `DeviceHarness` avoids this by calling `Start()` once + processing every command from inside that one `onBeforeCycle` lambda; `MachineHarnessScript.cpp` does same. `run <cpuCycles>` realized as "keep ticking half-cycles inside that lambda until this many whole CPU cycles complete", not as its own `Start()`/`Stop()` pair.

### The machine-handle model

Machine's registration shim (`<Machine>.machine-harness.cpp`) builds `Sim::Testing::MachineHandle` (`MachineHarnessCore.hpp`) exposing three verbs instead of per-pin stubs:

- **`Boot(romDir)`** loads ROM directory into machine's memory + performs machine's usual startup validation. Runs once, before `Start()`. Writes into memory device's storage, not netlist wiring, so safe before bake.
- **`TickHalfCycle()`** advances simulation by one half clock cycle: toggles machine's clock, holds or releases reset per any pending `HoldReset`, + ticks diagnostics. Per-iteration body of machine's own interactive `Start()` lambda minus keyboard polling. Returns `true` moment full CPU cycle completes, so runner can count CPU cycles without knowing machine's half-cycles-per-cycle ratio.
- **`HoldReset(halfCycles)`** starts holding reset asserted for next `halfCycles` calls to `TickHalfCycle()`. Does not block.

Named **inspection points** (`Sim::Testing::InspectionPoint`) each wrap `IRegisterInspectable*` and/or `IMemoryInspectable*`. Shim declares them, typically one for CPU + one for main memory.

## Script grammar

```
machine <Type> rom=<DIR> [key=value ...]

reset [halfcycles]
run <cpuCycles>
poke <point>[<addr>] = <value>
expect <point>.<register> = <value> [message words...]
expect <point>[<addr>] = <value> [message words...]
dump <point>
print <literal text to echo>
```

- **`machine`:** exactly one line per script (single-machine, like `DeviceHarness`'s single-device rule). `rom=<DIR>` required, passed to shim's `Boot()`.
- **`reset [halfcycles]`** calls `HoldReset`. Defaults to 10 half-cycles, convention this repo's machines use. Pass explicit count for machine needing different hold.
- **`run <cpuCycles>`** advances exactly that many whole CPU cycles. Incremental, not predicate engine. Firmware under test should park in halt loop after producing result, so bounded overshoot harmless + no "run until X" syntax needed.
- **`poke <point>[<addr>] = <value>`** one write verb (`IMemoryInspectable::Poke`), for test setup such as seeding RAM location before `run`.
- **`expect <point>.<register> = <value>`** + **`expect <point>[<addr>] = <value>`** assert named register (via `DumpRegisters()`) or one byte of memory (via `Peek`). No register-write verb: `IRegisterInspectable` read-only, so test controls register through what firmware does.
- **`dump <point>`** prints every register of point, or for memory point only its size in bytes (full memory dump too large to be useful). Use `expect <point>[<addr>]` for individual addresses.
- **`print <text>`** echoes literal line, no machine effect.
- **Output:** one line per command tagged `[c=<cpuCycles> t=<ms>]` (`c` is whole-CPU-cycle count, `t` is wall-clock milliseconds since run phase began), `PASS`/`FAIL` lines + final `N/M checks passed` line.
- **Comments:** line starting with `#` is comment, trailing `# ...` stripped, except `print` takes rest of line literally. Both harnesses share this lexing (`ScriptLexer.hpp`).

## Macros

`.machinetest` scripts support same `def <name> ... end` macros as `.devtest`; rules in [DeviceHarness.md](DeviceHarness.md#macros). Suit repeated `poke`/`run`/`expect` groups: one check per decoded address range, same assertions after each of several `run` steps, standard boot-and-settle prefix.

```
def EXPECT_RAM           # $0 = address, $1 = expected value
  expect ram[$0] = $1
end

EXPECT_RAM 0x200 0xA5
```

Arguments substituted as plain text, so `$0` may be pasted into longer word (`ram[$0]`); keep argument count exact.

## Registering a new machine

Machine's shim is colocated `<Machine>.machine-harness.cpp`, next to its other files:

```cpp
#include "../../Testing/MachineHarnessCore.hpp"
#include "MyMachine.hpp"

namespace {

Sim::Testing::MachineHandle CreateMyMachine(const Sim::Testing::ArgsMap&) {
	auto platform = std::make_unique<Sim::Platform>();
	auto machine = std::make_unique<MyMachine>(*platform);

	Sim::Testing::MachineHandle h;
	h.Type = "MyMachine";
	h.Points.emplace_back("cpu", Sim::Testing::InspectionPoint{.Registers = &machine->_cpu});
	h.Points.emplace_back("ram", Sim::Testing::InspectionPoint{.Memory = &machine->_ram});
	h.Boot = [m = machine.get()](const std::string& romDir) { m->LoadRom(romDir); };
	h.TickHalfCycle = [m = machine.get()] { return m->TickHalfCycle(); };
	h.HoldReset = [m = machine.get()](int halfCycles) { m->HoldReset(halfCycles); };
	h.PlatformOwner = std::move(platform);
	h.Owner = std::make_unique<Sim::Testing::OwnedMachine<MyMachine>>(std::move(machine));
	return h;
}

} // namespace

HARNESS_REGISTER_MACHINE("MyMachine", CreateMyMachine)
```

Machine class needs small, mechanical set of members to be eligible:

- Chips reachable from shim as `IRegisterInspectable&`/`IMemoryInspectable&`, either as public members (`&machine->_cpu`) or through accessors. Meaningful only once those devices implement interfaces (prior `device` skill step 4).
- `LoadRom(romDir)` that takes directory instead of hard-coded paths.
- `TickHalfCycle()` / `HoldReset(halfCycles)` pair, factored out of machine's interactive `Start()` loop so callable from inside harness's single `Platform::Start()`.

Machine built with `machine-engineer` skill has these from start. `src/Machines/BenEater6502/BenEater6502.machine-harness.cpp` + `BenEater6502.machinetest` are worked example.

`Sim::Testing::IOwnedMachine`/`OwnedMachine<T>` give handle virtual destructor that keeps machine alive, mirroring `IOwnedDevice`/`OwnedDevice<T>` for `DeviceHandle`.

## Where this fits

`MachineHarness` is `machine-engineer` skill's verification step for multi-device wiring, address decode + firmware boot/run behavior, which `DeviceHarness` cannot verify. Doesn't re-verify any single device's pin or register behavior; that's `DeviceHarness`'s job, done before device joins machine's component list.