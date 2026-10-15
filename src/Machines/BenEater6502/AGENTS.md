# AGENTS.md: BenEater6502

> **Scope:** machine facts for BenEater6502 only. Repo-level [../../../AGENTS.md](../../../AGENTS.md) cover sim core, device rules, workflow rules; read first. Design in [specs/specification.md](specs/specification.md).

Ben Eater breadboard 6502 computer, run his Microsoft BASIC port (`vendor/msbasic`, git submodule, unmodified), with HD44780 LCD, piezo on VIA PB7, serial console bridge to TCP.

## Build & run

Toolchain: same as repo level, plus `ca65`/`ld65`. Init submodule first: `git submodule update --init`.

```bash
src/Machines/BenEater6502/scripts/build-firmware.sh -o DIR       # prints DIR/rom/BenEater6502 (contains eater.bin)
src/Machines/BenEater6502/scripts/build-firmware.sh -t -o DIR    # the wiring-test image instead (see MachineHarness below)
```

`scripts/build-firmware.sh` copy `vendor/msbasic` to private work dir, call `Firmware/build.sh` (ca65/ld65). msbasic pick target with **lowercase** define (`ca65 -D eater`); `-D EATER` build wrong thing. No scratch entry file: firmware is vendored image. `-t` assemble `Firmware/machinetest.s` instead.

`BenEater6502` is interactive binary (separate target from `WireSim`, two machine headers cannot share one translation unit). CMake assemble firmware when build it, to `build/<preset>/rom/BenEater6502-basic` (default `--variant`) and `-machinetest`. `--rom DIR` load hand-built directory.

```bash
cmake --build --preset release --target BenEater6502
mkfifo in.fifo; sleep 100000 > in.fifo &                         # hold stdin open; kill that sleep by PID when done
build/release/BenEater6502 --cycles 2000000000 < in.fifo &
```

Keys on stdin: `s` status, `r` reset, `v` toggle verbose diagnostic (very slow, huge output; use short time), `d` open LCD window, `q` quit.

**Console:** TCP port 20022 (`socat - TCP:localhost:20022`, or `connect.sh 20022`). Reset land in WozMon at its `\` prompt. Type `8000R` to start BASIC, then Enter twice for `MEMORY SIZE?` and `TERMINAL WIDTH?`. End line with `\n` (`\r` before it ignored); console map it to `\r` firmware want, and turn firmware `\r` back to CRLF. `connect.sh` send Ctrl+C as `0x03`, stop running BASIC program, Ctrl+D disconnect. BASIC add `LCDCMD n`, `LCDPRINT expr`, `BEEP freq,duration`.

**Backspace:** console turn DEL (`0x7F`) into `0x08`. WozMon delete on `0x08`, but BASIC line editor ignore it, treat only `_` (`$5F`) as rubout, `@` kill line. Firmware still echo `0x08`, so host terminal cursor move back while typed chars stay in BASIC input buffer (`PRINT 11`, backspace, `2` run as `PRINT 112`). This is vendored firmware behavior, same as real machine. Type `_` in BASIC to delete char.

**Input speed:** host may type, paste, script input any speed. ACIA announce its RTS to console, hold received byte till RDR read (see `W65C51N.md`, "Flow control"), so nothing drop. Reset leave RTS deasserted, so console accept input only once WozMon program ACIA.

**MachineHarness:** BenEater6502 register `cpu` and `ram` points (EEPROM, ACIA, VIA, LCD not inspectable). `BenEater6502.machinetest` boot wiring-test image, not BASIC:

```bash
cmake --build --preset release --target check                     # or: ctest --test-dir build/release -R machinetest.BenEater6502
```

It cover top of SRAM, unmapped gap around ACIA and VIA window, ACIA and VIA register access, EEPROM read and ignored write, VIA `/IRQ` reach CPU. Not covered by script (check by hand over TCP console): ACIA receive and `/IRQ`, LCD, piezo.

## Machine facts

- **Memory map:** `$0000-$3FFF` SRAM (16 KiB), `$5000-$500F` ACIA, `$6000-$600F` VIA, `$8000-$FFFF` EEPROM, rest of `$4000-$7FFF` unmapped (undriven bus, read 0). Decode in `Chipset.hpp`. Class named `BenEaterChipset` so no clash with other machine `Chipset` in one binary. Only `RS` bits decode inside ACIA and VIA window (`$500B` is ACIA control register).
- **BASIC RAM:** `MEMORY SIZE?` probe upward from `$0400`, stop at first unmapped address (`$4000`), report `15359 BYTES FREE`. Never reach ACIA, so nothing stray send to console.
- **Clock/reset:** one shared `Phi2` (CPU's `CLKout`). Reset held low for first 10 half-cycles, fan out to CPU, ACIA, VIA.
- **Interrupts:** ACIA and VIA (`Variant::N`, open-drain) `IRQB` outputs share CPU `IRQB` with pull-up. Firmware enable only ACIA receive interrupt.
- **Serial flow control:** console clear-to-send is ACIA RTS (Command TIC bits, low while RDR empty) AND NOT VIA PA0, msbasic software RTS (high when its ring buffer near full). `SerialConsole` (`SerialConsole.hpp`) combine the two, its `Rts` pin linked to PA0.
- **EEPROM:** `AT28C256` take its image in constructor, so `LoadRom()` build and wire it (still before `Platform::Start()`).
- **LCD:** PB0-PB3 to DB4-DB7, PB4 RS, PB5 RW, PB6 E; backlight tied on. To see its write, toggle `v` around `LCDPRINT` and grep output for `LCD 16x2`.
- **ACIA inputs:** CTS, DSR, DCD grounded. VIA PA and PB idle low through pull-down.