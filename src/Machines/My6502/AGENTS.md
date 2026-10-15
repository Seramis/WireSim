# AGENTS.md: My6502

> **Scope:** machine-specific facts for My6502. Repo-level [../../../AGENTS.md](../../../AGENTS.md) cover simulator core, device convention and workflow rule; read first. Memory map in [MemoryMap.md](MemoryMap.md).

My6502 breadboard-style 6502 computer: W65C02 CPU, 64 KiB SRAM, 16C550 UART bridge to TCP, and debug peripheral. Run 6502 assembly (ca65/ld65) assemble to ROM image. Also machine where assembly test workflow run (`asm-test` skill, `sim-run.sh`).

## Build & run

Toolchain: same as repo level, plus `ca65`/`ld65`.

**Headless (preferred for agents):**

```bash
src/Machines/My6502/scripts/sim-run.sh -c 200                    # build, assemble Firmware/debug.asm in debug mode, run 200 CPU cycles
src/Machines/My6502/scripts/sim-run.sh -c 200 path/to/test.asm   # same, with your own test program (debug.asm untouched)
src/Machines/My6502/scripts/sim-run.sh -k -c 300000              # real kernel + WozMon, quiet mode
src/Machines/My6502/scripts/sim-run.sh -h                        # all options (-r release, -o workdir, -n skip build)
```

Script work in temp directory. Never touch `rom/`, `Firmware/debug.asm` or tracked listing. At cycle limit simulator print final `CPU state ...` line with register.

`sim-run.sh` assemble step wrap `scripts/build-firmware.sh` (plus `Firmware/build.sh`, ca65/ld65 half), firmware-build contract describe in `machine-engineer` skill. Call `build-firmware.sh` direct when need only ROM directory (example for `MachineHarness`):

```bash
src/Machines/My6502/scripts/build-firmware.sh -o DIR                     # debug.asm, DEBUG_MODE=1
src/Machines/My6502/scripts/build-firmware.sh -o DIR path/to/test.asm    # swap in a scratch program
src/Machines/My6502/scripts/build-firmware.sh -o DIR -k                  # real kernel + WozMon, DEBUG_MODE=0
```

Print `DIR/rom/My6502` on success. `DIR/obj/` hold assembler listing.

**Interactive / IDE:** CMake build and assemble three ROM variant next to executable, as `build/<preset>/rom/My6502-<variant>`: `kernel` (default), `debug` (`debug.asm` with cycle-tagged trace) and `machinetest` (see `machine.cmake`).

```bash
cmake --preset debug && cmake --build --preset debug --target WireSim
build/debug/WireSim --variant debug --cycles 1000       # traced debug.asm; or --rom DIR; any working directory
```

In VS Code, use CMake Tools Debug button with `WireSim` as launch target. Add `--variant debug` to `cmake.debugConfig.args` to debug traced `debug.asm` instead of kernel.

ld65 "Segment '…' does not exist" warning expected.

**MachineHarness:** My6502 expose public `_cpu`, `_ram` and `_uart` chip member, `LoadRom(romDir)` and `TickHalfCycle()`/`HoldReset()`, register through `My6502.machine-harness.cpp`. `W65C02`, `ParallelSram` and `ST16C550` implement inspection interface. `My6502.machinetest` and `My6502.machinetest.asm` boot machine and assert, through `cpu`, `ram` and `uart` point, that `Chipset` route Register8 write/read round trip, 16-byte indexed RAM fill-and-verify loop, and UART register write plus loopback FIFO fill-and-drain. `ctest` or `check` target run it; grammar in `src/Testing/MachineHarness.md`.

**Runtime gotcha:**

- `WireSim` **exit soon as stdin hit EOF**, so `< /dev/null` or closed pipe end run right away. Keep stdin open (script use FIFO) and always pass `--cycles`.
- Interactive key: `s` print counter, rate and CPU snapshot; `r` hold reset low 10 half-cycle; `g` open MRDI-1 graphics window (no-op if already open; can close/reopen); `l` open LED/switchbox window; `q` quit. BRK run normal 65C02 interrupt sequence, do not stop simulator.
- Startup fail with "ROM debug-mode marker missing" if ROM directory stale or empty. Rebuild with `cmake --build --preset <preset>`.
- If port 20021 busy, UART TCP server silent not start, but simulation still run.
- **Talk to firmware over UART:** start kernel background with big limit (`src/Machines/My6502/scripts/sim-run.sh -r -k -c 400000000 &`), then connect with `socat - TCP:localhost:20021`, or `./connect.sh 20021` for human. End input line with `\n` (`\r` before it ignored). Send `FFFC.FFFF\n` to WozMon return `FFFC: 69 C1 75 C1`.
- **Ad-hoc debug output from assembly:** `STA` byte to `$C009` (`HW::DBGWRITER`) and it append to `debug_writer.log` in working directory (truncate at startup), independent of `DEBUG_MODE` trace. See [DebugWriter.md](../../Devices/Debug/DebugWriter/DebugWriter.md).
- No unit-test suite exist. Verify mean build, run focused assembly program, check trace and final CPU state.

## Debug mode (`DEBUG_MODE`)

- `Firmware/main.asm` default `DEBUG_MODE = 0` unless define on command line (`ca65 -D DEBUG_MODE=1`).
  - `0` assemble real firmware (`kernel.s`, `wozmon.s`) run quiet.
  - `1` assemble `debug.asm` **instead of** kernel, enable cycle-tagged CPU/SRAM trace.
- Mode embed as marker `$4D $4F <mode>` at `$BFFD-$BFFF`, and `My6502::LoadRom` read it from loaded image.
- Prefer `-D DEBUG_MODE=1` (or `sim-run.sh`) over edit `main.asm`. If you edit it, restore after.

## Machine facts

- **Memory map:** all 64 KiB SRAM, preload from `rom/My6502/main.bank{0..3}.bin` at `$0000`, `$4000`, `$8000` and `$C000`. `Chipset.hpp` decode `$C000-$C007` to UART, `$C008` to `Register8`, `$C009` to `DebugWriter`, `$C00A-$C00B` to MRDI-1 display and `$C00C` to LED/switchbox, instead of SRAM. See [MemoryMap.md](MemoryMap.md). Firmware code start at `$C100`, vectors at `$FFFA-$FFFF`.
- **Clock:** one `Sim::Platform` cycle is one half clock cycle, `My6502` toggle clock before every cycle. Reset hold low first 10 half-cycle.
- **Trace tags:** `[h=<half-cycles> c=<cpu-cycles>]`. `P` print in binary; bit meaning in `W65C02/Flags.hpp`.