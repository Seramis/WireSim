# AGENTS.md: Z80Echo

> **Scope:** machine facts for Z80Echo only. Repo-level [../../../AGENTS.md](../../../AGENTS.md) covers sim core, device rules, workflow rules; read first.

Z80 computer echoing every byte received on its UART: Z80, 32 KiB AT28C256 EEPROM, 32 KiB AS6C62256 SRAM, ST16C550 UART bridged to TCP console. Firmware: Z80 assembly, sjasmplus.

## Build & run

Toolchain: repo level plus `sjasmplus` (v1.24.0 tested). Fedora doesn't package it: dev container builds it from source; elsewhere build a [release](https://github.com/z00m128/sjasmplus/releases) w/ CMake (`cmake -S . -B build -G Ninja && cmake --build build && cmake --install build`). Without it CMake warns, builds no Z80Echo firmware.

```bash
src/Machines/Z80Echo/scripts/build-firmware.sh -o DIR                 # prints DIR/rom/Z80Echo (contains z80echo.bin)
src/Machines/Z80Echo/scripts/build-firmware.sh -t -o DIR              # wiring-test image instead (see MachineHarness below)
src/Machines/Z80Echo/scripts/build-firmware.sh -o DIR scratch.asm     # scratch program instead of echo.asm
```

`scripts/build-firmware.sh` copies `Firmware/*.asm,*.inc` to private work dir, calls `Firmware/build.sh` (sjasmplus). Scratch program is copied into work dir only, may `include "hw.inc"` (ports, bit names) and `include "rom.inc"` (32 KiB `$FF`-filled image, end w/ `savebin ROM_BIN, 0x0000, 0x8000`). `DIR/obj/` holds listing (`<image>.lst`) and symbols (`<image>.sym`).

`Z80Echo` is its own binary (two machine headers can't share one translation unit). CMake assembles firmware on build, to `build/<preset>/rom/Z80Echo-echo` (default `--variant`) and `-machinetest`. `--rom DIR` loads hand-built directory.

```bash
cmake --build --preset release --target Z80Echo
mkfifo in.fifo; sleep 100000 > in.fifo &                          # hold stdin open; kill sleep by PID when done
build/release/Z80Echo --cycles 2000000000 < in.fifo &
```

Keys on stdin: `s` status, `r` reset, `v` toggle verbose diagnostic (very slow, huge output; keep short), `q` quit. Output buffered when redirected to file: appears at exit.

**Console:** TCP port 20023 (`socat - TCP:localhost:20023`, or `connect.sh 20023`). On connect console prints `Connected to UART on port 20023!`, then every byte typed comes back. End line w/ `\n`; console turns LF into CR LF host expects on output. Bulk paste safe: firmware keeps MCR.RTS asserted, UART throttles host through it when FIFO fills (4000 random bytes pasted echo back identical).

**MachineHarness:** Z80Echo registers `cpu`, `ram` (SRAM, index = address - `$8000`) and `uart` points (EEPROM not inspectable). `Z80Echo.machinetest` boots `Firmware/machinetest.asm`, not echo firmware. Covers SRAM address lines (cells checked in chip and by CPU read-back), EEPROM first/last cell and ignored write, UART registers through ports `$80-$87`, no UART response to ports outside window or to memory cycles at port-like address, UART `INT` reaching `/INT` (IM 1 handler at `$0038`, runs once):

```bash
cmake --build --preset release --target check                     # or: ctest --test-dir build/release -R machinetest.Z80Echo
```

Not covered by script (check by hand over TCP console): receive from socket, echo, flow control.

## Machine facts

- **Memory map:** `$0000-$7FFF` EEPROM (A15 low), `$8000-$FFFF` SRAM (A15 high). Reset vector `$0000` (`DI`, `LD SP,0`, jump to `main` at `$0100`); stack grows down from `$FFFF`. NMI vector `$0066` is a `RETN` (`/NMI` tied inactive).
- **I/O map:** UART at ports `$80-$87`, register select on A0-A2 (`hw.inc` names them). `Chipset.hpp` decodes only A3-A7 of I/O cycle (IORQ low, M1 high), so A8-A15 (accumulator or B on IN/OUT) ignored. IORQ w/ M1 low is interrupt acknowledge, selects nothing. Class named `Z80EchoChipset` to avoid clash w/ other machine's `Chipset` in one binary.
- **Strobes:** Z80 `/RD` and `/WR` gate memory and UART strobes directly (asserted only inside bus cycle), so chipset has no clock input. UART uses active-low `/IOR`, `/IOW`, `/CS2`; `CS0`, `CS1` tied high, active-high `IOR`, `IOW` tied low.
- **Clock/reset:** one shared `CLK`, one T-state per cycle. Reset held low first 10 half-cycles, fans out to Z80 `/RESET` and (inverted by chipset) UART `RESET`. `/WAIT`, `/NMI`, `/BUSREQ` tied inactive.
- **Interrupts:** UART `INT` (active high) inverted by chipset onto Z80 `/INT`. Echo firmware polls, never enables interrupts; `machinetest.asm` uses IM 1.
- **Console:** `TcpTerminal` on port 20023, null-modem linked to UART. Its clear-to-send is UART's RTS, so byte stream starts only once firmware sets MCR.RTS.
- **EEPROM:** `AT28C256` takes image in constructor, so `LoadRom()` builds and wires it (still before `Platform::Start()`). Image `z80echo.bin`, up to 32 KiB (shorter padded w/ `$FF`). Writes ignored.
- **SRAM:** `ParallelSram` w/ 15 address bits stands in for AS6C62256.
