@AGENTS.md

## Claude Code specifics

- parent `/home/joonatan/src/AGENTS.md` (C#/.NET rules) maybe load in context. skip it for this repo.
- proj skill (`.claude/skills/`): `asm-test`, `device`, `device-harness`, `machine-engineer`. `AGENTS.md` tell when use which.
- proj subagent (`.claude/agents/`): `device-debugger` (implement/audit/debug any device incl. CPUs via DeviceHarness), `machine-wiring-debugger` (chipset decode, enable fan-out, reset+interrupt wire across chips).
- run machine only with stdin held open and `--cycles` set, or go thru `src/Machines/My6502/scripts/sim-run.sh`. bare run die quick on stdin EOF or never stop.