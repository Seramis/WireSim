# CPU models

CPU is device (see [SKILL.md](SKILL.md)) verified by method in [coverage.md](coverage.md), driven through `DeviceHarness` as bus master (`device-harness` → `bus-master.md`), registers read through `IRegisterInspectable` (SKILL.md step 4). This file adds only what is specific to instruction-set architecture. Method names no opcode, flag or pin, so works for any architecture (6502, Z80, ...); architecture's own reference says what is correct, never implementation under test.

## Inventory additions (coverage.md step 1)

- Write down architecture's registers, flags, addressing modes, instruction groups, prefix pages (opcode pages selected by prefix bytes), interrupt kinds, reset behavior.
- Coverage row = one distinct (mnemonic, addressing mode, opcode page), with opcode(s), bytes, cycles. Group rows when ranges regular (e.g. register-select field).
- Extract implemented set from opcode table, decoder, prefix handlers. Unimplemented opcode decoding as another instruction or NOP counts as `missing`.
- Undocumented/illegal opcodes get explicit policy (implemented, trap, NOP) agreed with user.

## Skeleton for new CPU

Device class with pins, registers, `DumpRegisters`, harness shim, one reset+fetch script. `DumpRegisters()` returns every architectural register by manual name: program counter, stack pointer, accumulators/general registers, flags (one byte or named bits), plus alternate sets, index/segment registers, interrupt state (e.g. `IFF1`, `IM`) where architecture has them. Register missing from existing CPU: add it (add-only change, SKILL.md step 4). Nothing else until skeleton passes and bus profile recorded in `<Name>.md` (`bus-master.md` section 2).

## Instruction order (coverage.md step 2)

Typical dependency order; adapt to architecture:

1. Bus profile, reset sequence, opcode fetch, program counter advance.
2. No-op and cheapest observable instruction.
3. Immediate/register loads (let later tests set known state; with bus-cycle calls feeding data, also first thing to verify values reach registers).
4. Stores and moves (visible as write rows).
5. Flag-neutral control: jumps, calls/returns, branches (taken, not taken, page/boundary crossing).
6. Arithmetic/logic/shift/compare: results *and every flag*.
7. Stack, block/string, I/O instructions.
8. Prefix pages, one at a time.
9. Interrupts, reset-while-running, wait/halt states.

## Test design additions (coverage.md step 3)

- Observation for instruction is next instruction's fetch row: registers update when cycle latched, so that row exposes last cycle's effect. Then `expect reg.*` for all registers and flags.
- CPU-specific boundary values: negative displacements, stack wrap, half-carry, parity, page/segment crossing.
- Cover explicitly: reset (cycles, register effects, what left undefined); maskable and non-maskable interrupts (when sampled, edge vs level, masking, saved state, vector address, cycles); interrupt arrival during multi-cycle and prefixed instructions; wait/ready stalls (does bus repeat?); halt/stop states and what wakes them.

## Shared helpers

Addressing helpers, ALU, flag helpers, prefix dispatch shared by many opcodes. Fix or audit one: grep every call site, check each against mode it implements. Helper written for one mode (post-dereference indexing for `(zp),Y`) can silently be reused by mode needing opposite order (pre-dereference indexing for `(zp,X)`); look alike in `Enqueue` chain but compute different effective addresses.

## Reporting

On top of coverage.md final report: rows `verified` / `unverified` / `missing` with list of what is not verified, and plain statement when full instruction-set coverage not reached.
