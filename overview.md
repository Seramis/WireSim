# Simulator overview

WireSim netlist-based, event-driven sim. Split physical connect (**nets**) from function (**devices**).

All below in `namespace Sim` (`src/Simulator/`): `Sim::Platform`, `Sim::Pin`, `Sim::Bus`, `Sim::Device`, `Sim::TriggerMask`, `Sim::LogicState`, `Sim::Diagnostics`. Devices and machines (under `src/Devices/` and `src/Machines/`) stay global namespace, qualify with `Sim::`. `Sim::detail` holds internals (dirty-tag tracker, logic-resolution LUTs), not part of API.

## 1. Core architecture

- **Pins** are device interface. Pin can `Write()` drive value or `Read()` resolved net state.
  - Pin drive value **stateful**: holds till changed.
  - Writing pin changes only that pin's drive, no override of other pins on same net.
- **Nets** virtual wires joining pins. Net state = aggregate of all driving pins, resolved via **logic look-up table (LUT)**.
- **Devices** implement `Logic(TriggerMask mask)`. Sim resolves all nets batch, then runs all dirty devices batch, so read/write order inside `Logic()` don't matter. Device can't read back value just wrote same call, nets resolve only between device batches.

## 2. Logic states

Logic levels use 8-bit repr, handles contention and pull-up/pull-down:

- **Strong:** `H` (high), `L` (low).
- **Weak:** `Hw` (pull-up), `Lw` (pull-down).
- **Special:** `F` (floating, high-Z), `E` (error, contention).

Strong drives beat weak. Two diff strong drives (`H` and `L`) on one net gives `E`.

> Run loop no check for `E`. Contended net silent holds `E`, reads neither high nor low, so `Bus::Read` gives 0 for that bit. Look for contention explicit when debug bus fights.

## 3. The simulation cycle

Cycle got two phase: **immediate** (combinational) and **stable** (clocked/latched).

### Phase 1: immediate propagation

This phase settle combinational logic till circuit still.

1. **Net calculation:** every dirty net recompute state from pin drive values.
2. **Immediate triggers:** if net state change, its **immediate triggers** mark parent device dirty.
3. **Device execution:** all dirty devices run `Logic()` one batch. No nets recalc during batch, so every device see frozen state from step 1.
4. **Iteration:** steps 1-3 repeat till no net change.
5. **External commands:** work queued with `Platform::Enqueue()` runs after loop, before stable phase, then another immediate pass.

### Phase 2: stable execution

Once circuit stable, edge-sensitive and state-dependent logic runs.

1. **Stable triggers:** net that changed phase 1 and got **stable trigger** marks device dirty.
2. **Device execution:** dirty devices run `Logic()` **exactly once**.
3. **Finality:** writes made this phase resolve next cycle's immediate phase.

> **Stable writes lag one cycle.** True for every device. Pin written only from stable-triggered branch not visible thru `Pin::Read()` till next cycle's immediate phase. Test script `expect`, or other device reading that pin, must allow one extra cycle after edge that changed it.

## 4. Immediate vs. stable triggers

| Trigger | Use for | Execution |
| --- | --- | --- |
| **Immediate** | Combinational logic, address decoding, logic gates | Can run several times per cycle as signals ripple |
| **Stable** | Clock edges, write enables (`/WE`), strobes whose side effects are not idempotent | Runs at most once per cycle, after signals settle |

> **Example (SRAM):** address bus is **immediate** trigger so SRAM see address settle. Write enable is **stable** trigger so data latch only once address and data guaranteed valid.

Signature: `void AddTrigger(Sim::Device* device, uint8_t idBit = 0, bool isImmediate = true);`, or `template <typename TEnum> void AddTrigger(Sim::Device* device, TEnum idBit, bool isImmediate = true);`.

`TriggerMask` offer three check on bits `AddTrigger` registered:

- `IsSet(bit)`: one trigger fired. Takes raw `uint8_t` index or, better, device's own trigger enum.
- `IsSetAny(bits...)`: any of several fired (OR).
- `IsSetAll(bits...)`: all fired (AND).

Immediate trigger fire whenever its net *resolved*, not only value change (only stable triggers gated on change). Handler must read pin's current level when that matter, not guess from which trigger bit fired.

### Decode lag

When device react direct to signal (e.g. memory address bus) while `Chipset` decode same signal into device's enables (`/CE`, `/OE`), both land same immediate batch. Enable change take effect one iteration later, so device briefly run against old enable. Harmless: only stable-triggered devices (clock edge, write capture) act on result, run after loop converge. Real bug only if new immediate-triggered device reads bus mid-settle, trust glitch value. Don't infer bus timing from adjacent per-iteration trace lines. Only arise behind real `Chipset`, so machine concern.

## 5. Developer best practices

- **Avoid oscillations.** Zero-delay combinational feedback loop (e.g. inverter wired own input) loop forever or crash.
- **Use trigger mask.** Tells which pins caused call, so skip expensive work. Put mask check first in block that only react to specific triggers; cheaper than single pin read. Don't use it shortcut level check (CE, CS, RESET) that must reflect pin's current state every call. Read that pin direct, else call triggered by other pin while enable stay asserted sees stale output.
- **Release the bus.** Disabled device (e.g. `/CE` high) calls `Bus::Clear()` or writes `LogicState::F`.
- **Initialize with `Platform::Enqueue()`** to set initial states (such as pull-ups) before sim starts.

## 6. Debugging and inspection

Every machine executable takes `--cycles COUNT` to stop after fixed number of full CPU cycles. Hold stdin open when running non-interactive; sim exits on stdin EOF. Each machine's `AGENTS.md` lists its interactive keys, trace tags and debug options.

For structured, scripted inspect instead of reading trace by eye, device can opt into `Sim::IRegisterInspectable` / `Sim::IMemoryInspectable` (`src/Simulator/Inspect.hpp`). These two small freestanding interfaces, deliberate not part of `Sim::Diagnostics` (stays counters-only cuz passed to every device's hot `Logic()`). Queried only after sim stops. `DeviceHarness` and `MachineHarness` both read them. See `.claude/skills/device-inspect` for add them to device, and [src/Testing/MachineHarness.md](src/Testing/MachineHarness.md) for assert whole machine's state.