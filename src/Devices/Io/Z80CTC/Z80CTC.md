# Z80CTC

Zilog Z80 CTC counter/timer model (`docs/Zilog Z80 Peripherals.pdf`, "Counter/Timer Channels"). Four independent channels, each a timer (system clock via /16 or /256 prescaler) or event counter (`ClkTrg` edges). Z80 daisy-chain interrupt controller supplies per-channel vector in interrupt mode 2.

## Pinout reference

| Pin | Direction | Description |
| --- | --- | --- |
| `D` (8-bit bus) | Bidirectional | Data bus. Driven only during read, or interrupt acknowledge this device answers; else released. |
| `CS0`, `CS1` | Input, active high | Channel select, `CS1:CS0` = channel number. |
| `NotCE` | Input, active low | Chip enable. |
| `CLK` | Input | System clock. All counting and register writes on rising edge. |
| `NotM1` | Input, active low | Machine cycle one. With `NotIORQ`: interrupt acknowledge; with `NotRD`: opcode fetch. |
| `NotIORQ` | Input, active low | I/O request. |
| `NotRD` | Input, active low | Read strobe. No write pin: I/O cycle with `NotRD` high is a write. |
| `NotRESET` | Input, active low | Hardware reset. |
| `IEI` | Input, active high | Interrupt enable in, from next-higher-priority device (tie high for first in chain). |
| `IEO` | Output, active high | Interrupt enable out. High only while `IEI` high and no channel has request pending or under service. |
| `NotINT` | Output, open-drain, active low | Interrupt request. Low or floating, never driven high. |
| `ClkTrg0`-`ClkTrg3` | Input | Per-channel external clock (counter mode) or timer trigger (timer mode). Active edge selected per channel. |
| `ZcTo0`-`ZcTo2` | Output, active high | Zero count / time-out pulse, channels 0-2. Channel 3 has no pin. |

## Internal state reference

### Channel control word

Written with `D0` = 1 to channel address.

| Bit | Name | 0 | 1 |
| --- | --- | --- | --- |
| 7 | Interrupt | disabled | enabled: zero count requests interrupt |
| 6 | Mode | timer | counter |
| 5 | Prescaler (timer) | /16 | /256 |
| 4 | Edge | falling edge active | rising edge active |
| 3 | Trigger (timer) | starts when time constant loaded | starts on active `ClkTrg` edge |
| 2 | Time constant | none follows | next write to this channel is time constant |
| 1 | Reset | continue | stop channel (control bits unchanged; not stored state) |
| 0 | Control | vector word | control word |

### Time constant

8-bit, 0 means 256. Next write to channel after control word with bit 2 set is always time constant, whatever its `D0`.

### Vector

Write with `D0` = 0 (no time constant expected) stores bits 7-3 as vector base. Accepted at any channel address. Vector on bus is `base | channel << 1`, bit 0 zero.

### Down-counter

1-256, readable from channel address (low 8 bits, so 256 reads 0). Loaded from time constant, decremented, reloaded at zero.

## Behavior

### Bus cycles

Decoded from pins on every call, no clock needed:

| Cycle | Condition | Effect |
| --- | --- | --- |
| Write | `NotCE`, `NotIORQ` low, `NotM1`, `NotRD` high | One write per assertion, taken on first `CLK` rising edge that sees it (data read then). Deassert before next write. |
| Read | `NotCE`, `NotIORQ`, `NotRD` low, `NotM1` high | Drives selected channel's down-counter on `D`. Value latched when strobe (or channel select) starts, so decrement during cycle not seen. |
| Interrupt acknowledge | `NotM1`, `NotIORQ` low | If `IEI` high and request can be taken (see Interrupts): drives vector. Request moves to "under service" on first `CLK` rising edge in cycle. |
| Opcode fetch | `NotM1`, `NotRD` low, `NotIORQ` high | Byte on `D` sampled each rising edge; byte from last rising edge decoded after fetch ends (see RETI). |

### Counter mode

Active `ClkTrg` edge detected asynchronously, decrements down-counter on next `CLK` rising edge. Several edges between two rising edges count once. Edge in same cycle as rising edge counts on following one. Edges while channel stopped, or before time constant loaded, discarded. At zero: counter reloads, `ZcTo` pulses, and with bit 7 interrupt requested.

### Timer mode

Prescaler counts `CLK` rising edges, decrements down-counter each wrap (16 or 256 edges), so one period = prescaler × time constant edges. Without trigger bit, counting starts at first rising edge after the one that took time constant. With it, loaded channel waits for active `ClkTrg` edge, then first prescaler count is on second rising edge after it. Later trigger edges ignored while running.

New time constant written to running channel only replaces stored value: count in progress finishes, then reloads new one. Channel waiting for trigger takes new value into counter at once.

`ZcTo` goes high on rising edge reaching zero, low on next falling edge.

### Interrupts

Channel with bit 7 set requests interrupt on every zero count. `NotINT` low while `IEI` high and highest-priority channel (0 highest) with request is not blocked. Channel under service blocks itself and all lower channels, so higher channel can still interrupt lower one's routine. On acknowledge request becomes "under service" and vector driven.

`RETI` recognised from two consecutive opcode fetches, `ED` then `4D`, while `IEI` high: highest-priority channel under service ends service. Any other byte between, or different second byte (such as `45`), clears the `ED`.

`IEO` low while any channel has request pending or under service, so lower-priority devices cannot answer same acknowledge.

Clearing bit 7 withdraws unacknowledged request. Software reset (bit 1) leaves interrupt state alone.

### Reset

`NotRESET` is level, re-applied every call while low. All channels stop, control words (interrupt enables included) and time constants clear, requests and service end, `ZcTo` and `NotINT` go inactive, `IEO` follows `IEI`. Vector base kept. Constructor leaves device in this state.

### Output timing

`CLK`-triggered work runs in stable phase, so `NotINT`, `IEO`, `ZcTo` change one cycle after causing edge. `D` for reads and acknowledges is combinational.

## Register inspection

Implements `Sim::IRegisterInspectable`. `Vector` (bits 7-3), then per channel `Ch<n>.` `Control`, `TimeConstant` (raw byte), `Count` (1-256), `Prescaler`, `Running` (time constant loaded since last stop), `AwaitTc`, `IntPending`, `IntService`.

## Limitations

- **Timing edge-granular.** No setup/hold times: trigger edge coincident with rising edge treated as arriving just after it. Writes taken on first rising edge that sees strobe, not T3 edge datasheet names; counting starts one rising edge later, not at fixed offset inside next machine cycle.
- **No `NotM1` inhibit** of request changes during acknowledge. Request set by zero count mid-acknowledge simply queued.
- **Acknowledge needs clock.** Request moves to service on `CLK` rising edge; acknowledge strobe ending before one still drives vector but changes nothing.
- **Mode change without new time constant** on running channel keeps it running: counter becomes timer counting from current prescaler state, and vice versa. Datasheet does not define this.
- **Withdrawing request on interrupt disable**, vector writes to any channel address, vector base kept over reset: datasheet silent or describes only channel 0.
- **Power-up** is reset state, not undefined.
- **`IEI` must be driven.** Floating `IEI` reads low: device neither requests nor answers.

## Test coverage

`Z80CTC.devtest` covers:

- Reset state; channel select for control words on all four channels; writes ignored without `NotCE` and during acknowledge; one write per strobe across several rising edges.
- Time constant after control word (incl. even value not taken as vector, vector untouched).
- Timer /16: period to exact edge, reload, second period, no interrupt without bit 7, `ZcTo` pulse width. Timer /256 with time constant 0 (= 256).
- Counter read-back, incl. decrement inside read cycle, other channel's counter, 256 reading 0.
- Timer trigger start: edge before load ignored, wrong edge ignored, start on second rising edge, exact period, retrigger ignored.
- Counter mode, falling and rising edge: edges before time constant ignored, wrong edge ignored, clock alone never counts, count on next rising edge, coincident edge deferred, time constant 3 and 256 to exact edge, `ZcTo1` and `ZcTo2`.
- Software reset, reset with time constant follows, new time constant on running channel, control word on stopped channel.
- Interrupts: vector with channel bits, `NotINT` and `IEO` levels, request blocked while in service, RETI (and non-RETI sequences: `ED 45`, `ED` + other + `4D`, lone `4D`), priority and nesting across channels 1-3 and 0/2 simultaneous, `IEI` low (no request, no vector, no state change, RETI ignored), request withdrawn on interrupt disable, timer-mode interrupt.
- Hardware reset of running, interrupting channel; `IEO` following `IEI` in reset; vector kept; pending time constant forgotten.

Not exercised: timer trigger mode on channels other than 0; time constant rewrite while waiting for trigger; mode change on running channel; `ZcTo` and `ClkTrg` chained between channels (needs wiring).
