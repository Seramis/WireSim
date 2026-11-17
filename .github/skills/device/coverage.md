# Coverage-driven verification

Method for device with large behavior surface (many registers and modes, whole instruction set) or any task auditing existing device or claiming completeness. Applies to peripherals and CPUs alike; [cpu.md](cpu.md) adds ISA-specific parts. Verification itself is `DeviceHarness` (see `device-harness`); nothing here needs machine, ROM or assembler.

## Principles

- **Independent oracle.** Expected results come from reference, never implementation. Use, in order: datasheet/manual in `docs/` (`pdftotext -layout`), document user supplies, cited reference. None exists: ask user. Must use memory: mark every such expectation "unverified oracle" in coverage file and report.
- **Verify what you built on.** Test may rely only on behavior already verified for its setup (register that can be written and read back, instruction that loads known value). Order work so each group stands on previous one.
- **Nothing done until test says so.** Silent fallbacks (table's default entry, unimplemented register reading as zero, opcode decoding as another instruction or NOP) look implemented. Count only rows with passing test.
- **Fix the owner.** Fix device, never compensate in tests, memory or chipset. Fix touches helper shared by several registers, modes or instructions: audit every user. Helper right for one use often reused by another needing different operation order.

## 1. Inventory

1. Read reference and device's `<Name>.md`. Write down registers, bit fields, modes, interrupt/status sources, reset behavior (CPU: see `cpu.md`).
2. Create or update `<Name>.coverage.md` next to device: one row per distinct behavior (register × mode, or opcode × addressing mode × opcode page) with address/encoding, expected behavior, **status** (`missing` / `unverified` / `verified`), test file. Group rows when ranges regular, but list every encoding somewhere. Include undocumented, reserved or illegal encodings as rows with explicit policy (implemented, trap, ignored) agreed with user.
3. Extract implemented set from code (decode table, register switch, handlers); mark `missing` any encoding absent or aliased to fallback.
4. New device: first build skeleton (SKILL.md step 1: register-map skeleton or, for CPU, pins + registers + reset/fetch). Nothing else until it passes.

## 2. Order the work

Order groups by dependency: reset/idle state and cheapest observable behavior first, then groups letting later tests set known state, then groups depending on those. Peripheral: typically reset values, plain read/write registers, mode registers one at a time, then mode interaction (FIFO and interrupt priority, flow control, timing). CPU: `cpu.md` gives instruction order.

## 3. Design each test

Per behavior, one script section opened by `print TAG=<name>`:

- **Setup** using verified behavior only, putting registers and flags in known state. Set everything behavior must *not* change to opposite of expected final value, so "unchanged" observable.
- **Stimulus** with every cycle reference lists. Bus master: every bus cycle including dummy and internal cycles reaching bus (see `device-harness` → `bus-master.md`).
- **Observation** exposing last effect, then `expect` on **all** observable registers and flags, not only target.
- **Boundary values:** 0, 1, max, max-1, sign boundary, carry/borrow in and out, overflow, all-ones/zeros, address wraparound, page crossing, FIFO empty/full/threshold, each flag or status bit both set→clear and clear→set, each condition both taken and not taken.
- **Volume:** many vectors per behavior cheap if generated. Write throwaway script in scratch directory holding small model of semantics *written from reference*; have it emit macro calls and expected values, spot check several by hand against reference. Commit generated `.devtest`, not script.

## 4. Run, classify, fix

1. Write or generate and run group's `.devtest`. Run every `.devtest` in device's folder each time, as regression.
2. Classify every failure as script, macro or device bug (`bus-master.md` section 4: procedure for bus master). Re-read reference before touching device; log reference page or table used.
3. Fix smallest owning piece, auditing shared helpers as above.
4. Re-run reproducer and neighbouring boundary cases, then full folder.
5. Update `<Name>.coverage.md` statuses and `.md` "Test coverage" and known-deviation notes.

New behavior: write script first, watch it fail for right reason (not silent pass against `missing` fallback), implement, watch it pass.

## 5. Finish

Report group complete only when every row in it `verified`. Final report states: counts per status, anything marked "unverified oracle", known deviations from reference (documented, not silently accepted), what was not tested, exact commands run with pass/fail counts. Don't report whole device done while any row `missing` or `unverified`; list them.

Out of scope: chipset decode, address maps, firmware belong to `machine-engineer` / `machine-wiring-debugger`. Symptom appears only behind machine: say so, don't chase.
