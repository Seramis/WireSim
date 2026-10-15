---
description: "Use when debugging CPU or hardware behavior and simulator diagnostics, including W65C02 traces, SRAM transactions, My6502 run controls, or CA65 test programs."
applyTo: "src/**"
---
# Hardware Debugging

Use workflow for simulator diagnostics, hardware behavior bugs: 65C02 instruction results, bus transactions, cycle sequences, run control. Keep reproductions small. Separate CPU behavior from simulator scheduling, glue logic, memory behavior.

## W65C02 CPU model

Read this before touch CPU (`src/Devices/Cpu/W65C02/`).

- `W65C02::Logic` (the `Sim::Device` adapter, `W65C02.hpp`) copy pins into `Cpu::pins`, call `Cpu::Next()`, then drive A/D/RWB. `Cpu` (micro-op engine, `W65C02Impl.hpp`/`.cpp`) do work. Only CLK edge (stable trigger) do work.
- Every bus cycle = one **micro-op** (`void(*)(Cpu*)`) from 16-entry ring queue. Each micro-op invoked **twice**:
  1. **address phase** = first call, on PHI2 falling edge. `AddressPhase(addr, rw)` return `true`; drive address, return.
  2. **latch phase** = second call, at next falling edge. `LatchPhase(data)` return `true` with sampled data; update registers or state here.
- Instruction handlers in `*Ops.hpp` `Enqueue` sequence of micro-ops built from `Addressing::` helpers. `EnqueueFront` insert conditional extra cycles like page-cross fixups or RMW final write.
- `InstructionMap.hpp` map opcodes. **Every unmapped opcode default to `SystemOps::BRK`** (`table.fill(&SystemOps::BRK)`), so unimplemented opcode look like BRK (vector fetch from `$FFFE/$FFFF`) in trace, not crash. "Unimplemented Opcode" exit in `FetchOpCode` basically unreachable.
- Interrupt, reset priority decide in `DetermineNextInstruction()`: RESB, then NMI (edge), then IRQ (if I clear), then fetch.

## Reproduce with a machine

Assembly workflow below run on My6502 machine, which load four 16 KiB ROM images into SRAM at `$0000`, `$4000`, `$8000`, `$C000`, generate from `src/Machines/My6502/Firmware/main.asm`. Its `AGENTS.md` got build details, run controls, memory map. For machine without scratch-program path, like BenEater6502, reproduce through its `.machinetest` script.

- **Preferred:** write test to scratch `.asm` file, run `src/Machines/My6502/scripts/sim-run.sh -c COUNT test.asm`. Script build simulator, assemble in debug mode with your file in place of `Firmware/debug.asm`, run headless in temp directory, leave `rom/`, `debug.asm`, `DEBUG_MODE` untouched. If edit `Firmware/debug.asm` direct, record contents first, restore after. Keep test code at `.org $C100`.
- Keep each test focused on one instruction, addressing mode, flag transition or bus sequence; init every register, flag, memory location it rely on. `BRK` perform normal software interrupt sequence, resume at BRK+2, not stop simulator. Follow with signature byte, halt loop.
- For 16-bit zero-page pointer in `(zp,X)`, `(zp),Y` or `(zp)` modes, store low byte then high byte to consecutive addresses. Swap them give plausible but wrong effective address; double-check before blame simulator.
- `DEBUG_MODE` select image: `1` assemble `debug.asm` with cycle-tagged tracing, `0` assemble real kernel, run quiet. Default `0` in `Firmware/main.asm`, can override with `ca65 -D DEBUG_MODE=1`. Simulator read marker at `$BFFD-$BFFF`; rebuild all ROM banks after change modes.
- `build/debug/WireSim --cycles COUNT` stop after exactly `COUNT` full CPU cycles, print final `CPU state` dump. Exit immediate on stdin EOF, so hold stdin open when run non-interactive. Interactive keys list in My6502's `AGENTS.md`.
- For interactive use, `cmake --build --preset debug` build executable, assemble every firmware variant. Run `build/debug/WireSim --variant kernel` (or `--variant debug`) from any directory. In VS Code, use CMake Tools' Debug button. Use Release only when question depend on optimized behavior.

Minimal test shape (replace body with smallest failing case):

```asm
.org $C100

START:
    LDX #$FF
    TXS             ; known stack pointer
    ; Set up inputs and execute the behavior under test.
    BRK
    .byte $00       ; BRK signature byte (skipped by the return address)
DONE:
    JMP DONE        ; park so the final state stays stable

NMI:
    RTI

IRQ:
    RTI

.segment "VECTORS"
    .word NMI       ; NMI
    .word START     ; RESET
    .word IRQ       ; IRQ/BRK
```

Inspect state with final `CPU state` dump, interactive snapshot keys, or trace. Bound every run with `--cycles`; no rely on BRK as simulator stop.

## Read the trace correctly

Debug-mode CPU, SRAM messages carry half-cycle (`h`) and CPU-cycle (`c`) counts. CPU lines include PHI2, bus direction, address, data, reset/ready/interrupt inputs. SRAM read messages report CE/OE/WE, appear on read, address transitions; writes distinguish capture from commit. CPU snapshots include registers, flags, counters, bus pins. `P` print as bits; use `src/Devices/Cpu/W65C02/Flags.hpp` to interpret flag positions.

No infer fault just because adjacent CPU, SRAM lines no pair one-for-one. Correlate address, data, direction, enables, half-cycle order. CPU clock trigger stable-phase, while chipset, SRAM respond to immediate pin changes. `Cpu::Next()` advance on clock edges, separate address/output work from data-latch work across PHI1/PHI2. Quiet mode suppress per-cycle messages.

**Exact cycle counts:** no count `CPU PHI2=false` lines by hand. Look up each instruction's address in `DIR/obj/main.lst` (from `sim-run.sh -o DIR` run), then run `src/Machines/My6502/scripts/cycle-delta.sh DIR/trace.log ADDR1 ADDR2 [ADDR3...]` (lowercase hex addresses, in execution order) for delta between consecutive opcode fetches. Include every instruction boundary in region, even ones no care about (e.g. result-capturing `STA`); skip one merge two instructions' cycle counts.

**Datasheet:** `pdftotext -layout docs/w65c02s.pdf - | less` (without `-layout`, tables scramble). Reserved-opcode, NOP behavior, RMW timing, other NMOS-vs-CMOS differences in Table 7-1 "Microprocessor Operational Enhancements" (around pages 29-30). Check before derive timing for undocumented or reserved opcode.

If existing output no distinguish competing explanations, add temporary, narrow-scoped tracing without change signal behavior. Include monotonically increasing half-cycle count, PHI2 level or edge, CPU address, RWB, data in/out, reset/ready/interrupt inputs. For SRAM include CE/OE/WE, address, bus value, whether line read drive, write capture, or write commit. Log on transitions, not every settle invocation; remove or gate high-volume instrumentation after diagnosis.

**Ad-hoc output from test program:** to report intermediate value without parse trace or RAM dump, `STA` it to `$C009` (`HW::DBGWRITER`, `DebugWriter` device; see `src/Devices/Debug/DebugWriter/DebugWriter.md`). Each byte append to `debug_writer.log` (truncate at startup), independent of `DEBUG_MODE`'s trace.

## Isolate and fix the fault

1. State failing observable precise: final register or flag, wrong address or data, missing or extra transaction, or wrong cycle count. Record expected, actual behavior, and smallest assembly reproducer.
2. Build, run reproducer before change hardware logic. Check assembled listing, labels, reset vector if execution start at wrong address. Confirm ROM copies current (`sim-run.sh` always rebuild them), bound execution with `--cycles` or keyboard controls.
3. Trace transaction sequence by half-cycle. Establish first whether CPU drove wrong address, direction or data; whether chipset assert wrong selects or enables; whether SRAM drove or store wrong value; or whether CPU sample correct or incorrect bus value.
4. Follow owning code from `W65C02Impl::Cpu::Next()`, `DetermineNextInstruction()` into `InstructionMap.hpp`, relevant operation or addressing helper. Check both address, latch phases, when PC, registers, flags change, and page-crossing, wraparound, carry/borrow, 65C02-specific behavior. No assume every 65C02 opcode implemented; verify its table entry.
   - Addressing helpers (`Addressing::Zp`/`ZpX`/`Ind_Ptr`/`FixX`/`FixY`, etc.) share by many opcodes' `Enqueue` chains. When audit one, `grep` **every** call site, not just opcode you fix, check each against datasheet mode it implement. Helper write for one mode (post-dereference indexing for `(zp),Y`) can silent reuse by mode that need pre-dereference indexing (`(zp,X)`). Two look alike in `Enqueue` chain but compute different effective addresses.
5. Compare observed bus sequence, final architectural state with 65C02 behavior for that exact opcode, addressing mode. Change smallest owning operation or helper; no compensate for CPU defect in chipset or SRAM.
6. Re-run same program, add nearby boundary case if exercise distinct path. Keep cycle-tagged trace, report stop count, any requested CPU snapshot.

Simulator resolve nets between device batches, so device no read back value it wrote in same `Logic()` call. Trigger behavior in `overview.md`. Check this scheduling contract before treat same-call stale read as instruction-logic defect.

## Report back with

When trace no identify cause, give complete output from reset through cycle limit, exact assembly test, expected versus actual behavior, whether execution reach expected stop count or diverge earlier (for example unmapped opcode take BRK path). Include any added phase or control trace, smallest relevant CPU, chipset or SRAM code. No conclude from final register dump alone when suspect fault concern bus timing or interrupt sequencing.