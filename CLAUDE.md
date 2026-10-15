@AGENTS.md

## Claude Code specifics

- parent `/home/joonatan/src/AGENTS.md` (C#/.NET rules) maybe load in context. skip it for this repo.
- proj skill (`.claude/skills/`): `asm-test`, `device-cpp`, `device-verilog`, `device-harness`, `device-inspect`, `machine-engineer`. `AGENTS.md` tell when use which.
- proj subagent (`.claude/agents/`): `mos-stepper-debugger` (CPU/bus), `peripheral-device-debugger` (non-CPU device, register level), `machine-wiring-debugger` (chipset decode, enable fan-out, reset+interrupt wire across chips).
- run machine only with stdin held open and `--cycles` set, or go thru `src/Machines/My6502/scripts/sim-run.sh`. bare run die quick on stdin EOF or never stop.