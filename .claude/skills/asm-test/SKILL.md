---
name: asm-test
description: Write and run a focused 65C02 assembly test program on the WireSim machine, then read the trace and final CPU state. Use when verifying an instruction, addressing mode, flag behaviour, interrupt sequence or bus timing, or when reproducing a CPU/SRAM/chipset bug.
---
# Run a focused 65C02 test on WireSim

Workflow run on My6502 machine. Machine load scratch programs via `sim-run.sh`, have traced `debug` firmware variant. Other machines run fixed firmware, no scratch-program path.

## 1. Write the program in a scratch file

Do **not** edit `src/Machines/My6502/Firmware/debug.asm` or `DEBUG_MODE` in `Firmware/main.asm`. Write test to scratch `.asm` file outside repo (or under session scratchpad).

```ca65
.org $C100

START:
    LDX #$FF
    TXS             ; known stack pointer
    ; Initialise every register, flag and memory cell the test relies on.
    ; Put the single behaviour under test here.
    BRK
    .byte $00       ; BRK signature byte: the return address skips it
DONE:
    JMP DONE        ; park so the final CPU dump is stable

NMI:
    RTI

IRQ:
    RTI             ; BRK and IRQ share the $FFFE vector

.segment "VECTORS"
    .word NMI       ; $FFFA
    .word START     ; $FFFC RESET
    .word IRQ       ; $FFFE IRQ/BRK
```

Rules:

- Test one instruction, addressing mode, flag transition or bus sequence per program. Put boundary cases (page cross, zero-page wrap, carry/borrow, decimal mode, N/Z/V edges) in separate programs or separated blocks clearly.
- Make results observable. Leave result in A/X/Y/P for final dump; or store to RAM (e.g. `STA $0200`) so show as `SRAM commit W 0200:xx` in trace; or `STA $C009` append byte to `debug_writer.log` thru `DebugWriter` device, tag value at specific point in loop without dig it out of trace (see `src/Devices/Debug/DebugWriter/DebugWriter.md`).
- `$C000-$C007` is UART, not RAM. Do not use for data.
- Without `JMP DONE` halt loop, BRK return into whatever follow it.
- To set up 16-bit zero-page pointer for `(zp,X)`, `(zp),Y` or `(zp)` tests, store low byte first, then high byte, to consecutive addresses (`STA $10` then `STA $11` for pointer at `$10`). Swap them give plausible but wrong effective address. Check against intended target before call mismatch simulator bug.

## 2. Run it

```bash
src/Machines/My6502/scripts/sim-run.sh -c 120 /path/to/test.asm > /path/to/out.txt 2>&1
```

- Size `-c` to cover reset (about 9 cycles), program, BRK (7 cycles) and few halt-loop iterations. Overshoot fine, halt loop keep state stable.
- Use `-o DIR` to keep fixed work directory. `DIR/obj/main.lst` and `DIR/obj/labels.lst` show assembled bytes and label addresses.
- Debug trace verbose (about 5 lines per CPU cycle). Redirect to file, grep it, no print it.

## 3. Read the results

- **Final line:** `CPU state c=… PC=… A=… X=… Y=… S=… P=<binary> …`. Flag bits, MSB to LSB: `N V 1 B D I Z C` (see `src/Devices/Cpu/W65C02/Flags.hpp`).
- **Bus sequence:** `grep 'CPU PHI2=false'` give one line per CPU cycle, with address driven for upcoming cycle and R/W.
- **Memory traffic:** `SRAM drive R` (read), `SRAM capture W` and `SRAM commit W` (write).
- **Three stack pushes then vector read from `$FFFE/$FFFF` where you no expect BRK** usually mean unmapped opcode. Every unmapped entry in `InstructionMap.hpp` fall back to `SystemOps::BRK`, skip one operand byte. Check table before suspect instruction logic.
- **Exact cycle counts:** look up each instruction address in `DIR/obj/main.lst`, then run `src/Machines/My6502/scripts/cycle-delta.sh DIR/trace.log ADDR1 ADDR2 [ADDR3...]` (lowercase hex addresses such as `c10f`, in execution order). Print cycle count between consecutive opcode fetches, so no need count `CPU PHI2=false` lines by hand. Pass every instruction boundary in region, include ones you no care about (e.g. `STA` that capture result). Delta only meaningful between *consecutive* opcode fetches; skip one fold two instructions cycles together.
- **Datasheet:** compare cycle counts and bus sequences with `docs/w65c02s.pdf`. No infer timing from adjacent lines; correlate by `h=` and `c=` counters. Search with `pdftotext -layout docs/w65c02s.pdf - | less` (without `-layout`, tables scramble). Addressing-mode cycle counts in per-instruction tables. Reserved-opcode and NOP behavior, RMW timing and other NMOS-vs-CMOS differences in **Table 7-1 "Microprocessor Operational Enhancements"** (around pages 29-30). Check before derive timing for undocumented or reserved opcode.

## 4. Report

State exact program, `-c` value, expected and observed final state and bus sequence, and whether run reach cycle limit. If you change simulator code, re-run same program plus one adjacent boundary case after fix.