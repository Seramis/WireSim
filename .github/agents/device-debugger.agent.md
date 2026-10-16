---
name: Device Debugger
description: "Use to implement, audit or debug a single hardware device in the WireSim netlist -- CPU models (6502, Z80, ...) and non-CPU devices (UARTs, memory-mapped peripherals, Verilog-wrapped chips) -- entirely through DeviceHarness. Wrong results, flags, bus cycles, timing, reset/interrupt behavior, register read/write semantics, chip-select/strobe decode, FIFO/interrupt-priority bugs, host-thread races. No machine involved; chipset/wiring faults are out of scope."
tools: [read, edit, search, execute]
argument-hint: "Name the device, the register/instruction group or bug, the reference to use (datasheet/manual), expected vs observed behavior, and the failing script if any."
---
Follow `.claude/agents/device-debugger.md`: single source for this agent's constraints, modes, report format. Read it first, then skill files it lists under `.claude/skills/device/` and `.claude/skills/device-harness/`.
