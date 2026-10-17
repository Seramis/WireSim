# Z80SIO

Zilog Z80 SIO dual-channel serial controller model (`docs/Zilog Z80 Peripherals.pdf`, "Serial Input/Output"). Each channel is full-duplex asynchronous transmitter and receiver driven by external bit clocks, with modem control lines. Channel can instead link to byte-stream endpoint (e.g. TCP console), no clocks at all. Z80 daisy-chain interrupt controller prioritizes six sources, supplies vector in interrupt mode 2. Synchronous, monosync/bisync and SDLC/HDLC modes not modeled (see Limitations).

## Pinout reference

Full signal set of 44-pin package: every clock, `DTR` and `SYNC` of both channels available.

| Pin | Direction | Description |
| --- | --- | --- |
| `D` (8-bit bus) | Bidirectional | Data bus. Driven only during read, or interrupt acknowledge this device answers; else released. |
| `BNotA` | Input | Channel select: high = B, low = A. |
| `CNotD` | Input | Port select: high = control/status, low = data. |
| `NotCE` | Input, active low | Chip enable. |
| `CLK` | Input | System clock. Bus writes, read side effects, interrupt acknowledges on rising edge. Serial engines do not use it. |
| `NotM1` | Input, active low | Machine cycle one. With `NotIORQ`: interrupt acknowledge; with `NotRD`: opcode fetch. |
| `NotIORQ` | Input, active low | I/O request. |
| `NotRD` | Input, active low | Read strobe. No write pin: I/O cycle with `NotRD` high is a write. |
| `NotRESET` | Input, active low | Hardware reset of both channels. |
| `IEI` | Input, active high | Interrupt enable in, from next-higher-priority device (tie high for first in chain). |
| `IEO` | Output, active high | Interrupt enable out. High only while `IEI` high and no source has request pending or under service. |
| `NotINT` | Output, open-drain, active low | Interrupt request. Low or floating, never driven high. |
| `RxD<ch>` | Input | Receive data. |
| `TxD<ch>` | Output | Transmit data. High (marking) when idle; changes on falling edge of `TxC`. |
| `RxC<ch>` | Input | Receive clock. Data sampled on rising edge. |
| `TxC<ch>` | Input | Transmit clock. |
| `NotCTS<ch>`, `NotDCD<ch>` | Input, active low | Modem status inputs. Optionally transmitter and receiver enables. |
| `NotSYNC<ch>` | Input, active low | In asynchronous modes a third modem status input. |
| `NotRTS<ch>`, `NotDTR<ch>` | Output, active low | Modem control outputs. |
| `WRdy<ch>` | Output | Wait/Ready. Open-drain Wait (floating or low) or driven Ready (high or low), per WR1. Floating after reset. |

`<ch>` is `A` or `B`. Pins in `Pins.A` and `Pins.B` (`ChannelPins`).

## Internal state reference

### Register access

`BNotA` selects channel, `CNotD` port.

| `CNotD` | Write | Read |
| --- | --- | --- |
| low | Transmit data | Receive data |
| high | WR0, or register named by pointer | Register named by pointer |

Every channel has pointer, 0 after reset. Control-port write with pointer at 0 is WR0: bits 2-0 stored as pointer, bits 5-3 command, bits 7-6 CRC command. Next control access, read or write, uses register the pointer names; afterwards pointer returns to 0. Command in same byte runs before pointer stored, so pointer bits sent with channel reset take effect.

### Write registers

| Reg | Contents |
| --- | --- |
| WR0 | Pointer (bits 2-0), command (bits 5-3), CRC reset code (bits 7-6). |
| WR1 | Bit 0 external/status interrupt enable. Bit 1 transmit interrupt enable. Bit 2 status affects vector (channel B only). Bits 4-3 receive interrupt mode. Bit 5 Wait/Ready on receive (1) or transmit (0). Bit 6 function: Ready (1) or Wait (0). Bit 7 Wait/Ready enable. |
| WR2 | Interrupt vector. Channel B only; write through channel A ignored. |
| WR3 | Bit 0 receiver enable. Bit 5 auto enables. Bits 7-6 receive bits per character: 00 = 5, 01 = 7, 10 = 6, 11 = 8. Other bits stored, unused. |
| WR4 | Bit 0 parity enable. Bit 1 even parity. Bits 3-2 stop bits: 01 = 1, 10 = 1½, 11 = 2, 00 = synchronous mode. Bits 7-6 clock mode: 00 = x1, 01 = x16, 10 = x32, 11 = x64. Bits 5-4 (sync mode) stored, unused. |
| WR5 | Bit 1 RTS. Bit 3 transmitter enable. Bit 4 send break. Bits 6-5 transmit bits per character: 00 = 5 or fewer, 01 = 7, 10 = 6, 11 = 8. Bit 7 DTR. Bits 0 and 2 (CRC) stored, unused. |
| WR6, WR7 | Sync characters. Stored, unused. |

Write WR4 before other parameters. After reset it selects synchronous mode, where neither engine runs.

### Commands (WR0 bits 5-3)

| Value | Command |
| --- | --- |
| 0 | Null. |
| 1 | Send Abort. No effect in asynchronous modes. |
| 2 | Reset External/Status Interrupts: clears request and latch (see External/Status). |
| 3 | Channel Reset: resets this channel as `NotRESET` does. Channel A also ends every service in progress. WR2 kept. |
| 4 | Enable Interrupt on Next Receive Character. |
| 5 | Reset Transmitter Interrupt Pending. |
| 6 | Error Reset: clears latched parity and overrun flags and flags of character at head of receive FIFO. |
| 7 | Return from Interrupt: ends highest-priority service. Channel A only; channel B ignores it. |

CRC reset code 3 (bits 7-6 = 11) clears Transmit Underrun/EOM latch. Other codes null.

### Read registers

| Reg | Bit | Meaning |
| --- | --- | --- |
| RR0 | 0 | Receive character available. |
| | 1 | Interrupt pending: any source requesting, blocked or not. Channel A only, 0 in B. |
| | 2 | Transmit buffer empty. Set after reset. |
| | 3 | DCD: inverted `NotDCD` pin. |
| | 4 | Sync/Hunt: inverted `NotSYNC` pin. |
| | 5 | CTS: inverted `NotCTS` pin. |
| | 6 | Transmit Underrun/EOM latch. Set after reset, cleared only by CRC reset code. |
| | 7 | Break detected. |
| RR1 | 0 | All sent: transmit buffer and shift register both empty. |
| | 3-1 | Residue codes, 0. |
| | 4 | Parity error. |
| | 5 | Receive overrun. |
| | 6 | Framing error. |
| | 7 | End of frame, 0. |
| RR2 | 7-0 | Channel B only: vector as an acknowledge would return it now (see Interrupts). |

RR0 bits 3-7 are live while external/status interrupts disabled and between latched change and its reset; see External/Status.

### Interrupt vector

With WR1 bit 2 (channel B) clear, acknowledge returns WR2 as written. With it set, V3-V1 replaced by source code, V7-V4 and V0 stay as written:

| V3 V2 V1 | Source |
| --- | --- |
| 000 | Channel B transmit buffer empty |
| 001 | Channel B external/status |
| 010 | Channel B receive character |
| 011 | Channel B special receive condition |
| 100 | Channel A transmit buffer empty |
| 101 | Channel A external/status |
| 110 | Channel A receive character |
| 111 | Channel A special receive condition |

RR2 with nothing pending reads V3-V1 = 011.

## Behavior

### Bus cycles

Decoded from pins on every call, no clock needed:

| Cycle | Condition | Effect |
| --- | --- | --- |
| Write | `NotCE`, `NotIORQ` low, `NotM1`, `NotRD` high | One write per assertion, taken on first `CLK` rising edge that sees it (data read then). Deassert before next write. |
| Read | `NotCE`, `NotIORQ`, `NotRD` low, `NotM1` high | Drives selected register. Value latched when strobe (or selection) starts, so character arriving during cycle not seen. Data read removes character, and any control-register read returns pointer to 0, on first `CLK` rising edge of cycle. |
| Interrupt acknowledge | `NotM1`, `NotIORQ` low | If `IEI` high and request can be taken: drives vector. Request moves to "under service" on first `CLK` rising edge in cycle. |
| Opcode fetch | `NotM1`, `NotRD` low, `NotIORQ` high | Byte on `D` sampled each rising edge; byte from last rising edge decoded after fetch ends (see RETI). |

Data-port write while transmit buffer full replaces its contents. Data read with receive FIFO empty returns 0.

### Transmitter

Character written to data port waits in buffer (RR0 bit 2 clear). When transmitter idle and enabled (WR5 bit 3; with WR3 bit 5 also `NotCTS` low), next falling edge of `TxC` moves it to shift register, empties buffer, starts frame. Frame: start bit (low), data bits LSB first, parity bit if enabled, stop bits. Bit lasts 1, 16, 32 or 64 falling edges (clock mode); stop bits last that many times 1, 1½ or 2. If buffer holds another character when frame ends, its start bit follows on edge after last stop-bit edge; otherwise line stays high and All Sent (RR1 bit 0) set.

Data bits per character: 8, 7 or 6 take low bits of written byte, ignore rest. "5 or fewer" takes count from leading ones in bits 7-4 of byte: none = five data bits, one to four ones = four down to one. So `0x15` sends five bits, `0x8A` four, `0xC6` three, `0xE2` two, `0xF1` one. Parity computed over data bits sent: parity bit makes ones count even (WR4 bit 1 set) or odd.

Transmitter disabled during frame completes that frame. Send Break (WR5 bit 4) forces `TxD` low at once, releases it to transmitter output when cleared.

`NotRTS` goes low when RTS bit set. When bit cleared it stays low until All Sent, then goes high. `NotDTR` follows DTR bit.

### Receiver

With receiver enabled (WR3 bit 0; with WR3 bit 5 also `NotDCD` low), each `RxC` rising edge looks at `RxD`. Low sample while idle starts character. In x16, x32, x64 start bit confirmed half bit time (8, 16 or 32 edges) later, high level there is glitch and ignored; every later bit sampled one bit time after previous sample, near bit middle. In x1 no way to find middle: first low sample taken as valid start bit, each following edge is one bit.

Character complete when first stop bit sampled. Characters shorter than eight bits read back with ones in unused high bits; with parity enabled received parity bit sits just above data (5-bit character with parity is `11 P D4..D0`). Eight-bit character with parity checks parity bit, does not return it. Parity error is mismatch with selected sense. Low stop bit is framing error; receiver then waits for line to go high before looking for new start bit.

Completed characters go into three-character FIFO, each with parity, framing, overrun flags. Fourth character replaces third and carries overrun flag. RR0 bit 0 set while FIFO holds character. RR1 shows flags of head character, plus parity and overrun flags latched from characters already read; both latches stay until Error Reset. Framing flag is head character's only. To keep flags paired with data, read RR1 before data.

Break is null character (all data bits low) with framing error. RR0 bit 7 sets when detected, clears when `RxD` next samples high. Both changes request external/status interrupt. Null character also in FIFO.

Disabling receiver returns it to idle, clears Break without interrupt.

### External/Status

With WR1 bit 0 set, change on `NotCTS`, `NotDCD` or `NotSYNC`, or start/end of break, latches RR0 bits 3-7 as after the change and requests interrupt. Further changes not seen while latched. Reset External/Status (command 2) clears request and latch: RR0 shows live values again, next change requests again. Acknowledge ends request but leaves latch. With WR1 bit 0 clear nothing latches, RR0 bits 3-7 always live; clearing bit while latched releases latch and withdraws request.

### Interrupts

Six sources, priority order: channel A receive, A transmit, A external/status, B receive, B transmit, B external/status. Source under service blocks itself and every lower one, so higher source can still interrupt lower one's routine.

- **Receive** (WR1 bits 4-3): 00 disabled. 01 first character only. 10 all characters, parity error is special condition. 11 all characters, parity error not special. While FIFO holds character source requests; acknowledge moves it to service, and if character still there when service ends it requests again. Character whose flags include framing error or overrun (or parity error in mode 10) is special receive condition, gives special vector. In mode 01 plain request made once: armed by selecting mode and by command 4, acknowledge disarms it. Special conditions (framing, overrun) request in mode 01 whether armed or not, and hold head character in FIFO, so data reads do not remove it, until Error Reset.
- **Transmit** (WR1 bit 1): requests when buffer empties because character moved to shift register. Does not request merely because interrupt enabled while buffer empty. Withdrawn by acknowledge, command 5, writing a character, or clearing the enable.
- **External/status** (WR1 bit 0): see above.

`NotINT` low while `IEI` high and highest-priority source not blocked. Acknowledge cycle with `IEI` high drives vector for that source, then source goes under service; with nothing requesting, or `IEI` low, nothing driven and nothing changes.

`RETI` recognised from two consecutive opcode fetches, `ED` then `4D`, while `IEI` high: highest-priority source under service ends service. Any other byte between, or different second byte (such as `45`), clears the `ED`. Command 7 in channel A does same without needing bus or `IEI`.

`IEO` low while any source has request pending or is under service.

### Wait/Ready

Per channel, set by WR1 bits 7-5. With enable (bit 7) clear, `WRdy` is high for Ready function, floating for Wait. With it set:

- **Ready** (bit 6 set): low while buffer can be served (transmit: buffer empty; receive: character waiting), high otherwise, and high during any CPU I/O cycle.
- **Wait** (bit 6 clear): low during data-port transfer buffer cannot satisfy: for receive (bit 5 set) read while FIFO empty, for transmit write while buffer full. Otherwise floating. Responds to transfer on either channel's data port.

### Byte link

Channel can move from serial pins to byte-level transport. Register interface unchanged; only changes what happens to character after it leaves transmit buffer and before it reaches receive FIFO.

- `BindByteLink(ch, {transmit, ready})` binds channel to callbacks. `transmit(byte)` called for each character sent; `ready(bool)` called when channel's RTS output (level `NotRTS` shows, active = true) changes. Characters offered with `HostReceive(ch, byte, wait)` from any thread.
- `HostTerminal(ch)` returns `Terminal` endpoint for channel and binds it, so `Terminal::NullModemConnect(&tcpTerminal, &sio.HostTerminal(0))` connects channel to `TcpTerminal`. Peer's clear-to-send follows `NotRTS`, so `TcpTerminal` reads its socket only once CPU has set RTS (WR5 bit 1). Peer's RTS not read. Bind before simulation starts.

On linked channel `RxC`, `TxC`, `RxD` ignored and `TxD` stays high; no frames, so parity, framing, break, overrun never occur, and clock mode, stop bits, parity bits of WR4 have no effect. Everything else works: registers, interrupts, modem inputs and outputs, auto enables, Wait/Ready.

- **Transmit.** On first `CLK` rising edge after transmitter enabled and buffer holds character (WR5 bit 3, WR4 not in synchronous mode, `NotCTS` low when WR3 bit 5 set), character passed to `transmit`, buffer empties, transmit interrupt requested if enabled. Only data bits count: 8, 7 or 6 take low bits of byte, "5 or fewer" takes count from leading ones as in serial transmission.
- **Receive.** Characters offered by host wait in four-character queue, move into three-character FIFO on each `CLK` rising edge as room appears, so slow CPU loses nothing and order kept. `HostReceive` with `wait` blocks while queue full, pushing back on `Terminal` relay; without `wait` returns false. Characters shorter than eight bits read back with ones above data. While receiver not active (WR3 bit 0 clear, WR4 in synchronous mode, or auto-enable with `NotDCD` high) queued characters discarded.
- **Timing.** Character written to data port taken on one rising edge, sent on next, and, if looped back, in FIFO on that same edge. Host characters reach FIFO on first rising edge after queued.

#### Threading and locking

`HostReceive` may be called from `Terminal` worker thread. Mutex guards host queue, condition variable wakes blocked `HostReceive` when sim thread drains it; sim thread checks atomic queue length first, so rising edge with nothing queued takes no lock. Nothing but that queue touched from other thread: pins and registers written only from `Logic()`. `transmit` and `ready` called on sim thread; for `Terminal` they only queue a byte or update peer's clear-to-send flag. Destructor wakes any blocked `HostReceive` before terminals' threads joined.

### Reset

`NotRESET` is level, re-applied every call while low. Both channels return to reset state: registers and pointer clear, engines stop, `TxD` high, `NotRTS` and `NotDTR` high, `WRdy` floating, FIFO empties, RR0 reads transmit buffer empty and underrun set, requests and services end, `NotINT` inactive, `IEO` follows `IEI`. WR2 kept. Constructor leaves device in this state. Channel Reset (command 3) does same for one channel.

### Output timing

Bus-cycle work runs on `CLK`, serial engines on `RxC`, `TxC` and modem inputs, all in stable phase, so `TxD`, `NotRTS`, `NotINT`, `IEO`, `WRdy` change one cycle after causing edge. `D` for reads and acknowledges, and Wait output, combinational. `RxD` read in cycle where rising `RxC` edge seen, so must be settled before edge.

## Register inspection

Implements `Sim::IRegisterInspectable`. `Vector` (WR2), `InService` and `Requests` (6-bit masks, bit = channel × 3 + 0 receive, 1 transmit, 2 external/status; channel A first), then per channel `A.` and `B.`: `Pointer`, `WR1`, `WR3`-`WR7`, `RR0`, `RR1` (as a read would return, no side effects), `RxCount`, `TxBufFull`, `TxActive`, `Break`, `ByteLinked`, `HostQueued` (characters waiting in host queue).

## Limitations

- **Asynchronous modes only.** With WR4 bits 3-2 = 00 (state after reset) transmitter holds `TxD` marking and receiver stays idle. Monosync, bisync, external sync, SDLC/HDLC absent: no sync character matching or insertion, hunt phase, CRC generation or checking, zero insertion, flags, abort, address search, residue handling. WR6, WR7 and CRC and sync bits of WR3-WR5 stored, unused; RR1 residue and end-of-frame bits read 0; Send Abort and CRC commands (other than underrun latch) do nothing. `NotSYNC` input only. Transmit Underrun/EOM latch set by reset, never set again.
- **Timing clock-edge granular.** No setup, hold or pulse-width requirements: modem inputs change status on any transition. Start bit found on first `RxC` rising edge sampling low, up to one `RxC` period after real falling edge. Bus writes and read side effects on first `CLK` rising edge that sees strobe, not datasheet's T-state offsets; strobe ending before rising edge still drives its value but changes nothing. Datasheet's four-clock wait after channel reset not enforced.
- **Receiver framing.** One stop bit checked, whatever WR4 selects; second stop bit is just idle line. Extra half bit delay after framing error replaced by waiting for line to go high. 1½ stop-bit transmit frame in x1 mode lasts one clock.
- **Error flags.** Overrun flag marks character that replaced the third, replaced character lost. RR1 shows latches and head character's flags; Error Reset also clears head character's flags. Datasheet does not say how its per-character flag FIFO interacts with Error Reset.
- **Registers datasheet leaves undefined.** Pointer values 3-7 on read, and pointer 2 on channel A, read RR0. Read of empty receive FIFO returns 0. Data write over full transmit buffer overwrites it. Writing receive-interrupt mode 01 arms first-character interrupts; datasheet gives only command 4. Enabling transmit interrupt while buffer empty requests nothing, as datasheet's programming example implies. WR2 kept over reset.
- **Wait/Ready** follows datasheet's table, not its cycle timing. Datasheet does not say whether Wait applies to reads or writes of the buffer in question; here receive Wait applies to reads, transmit Wait to writes.
- **No `NotM1` inhibit** of request changes during acknowledge. Request appearing mid-acknowledge simply queued. Acknowledge needs `CLK` rising edge to move request to service.
- **Power-up** is reset state, not undefined.
- **`IEI` must be driven.** Floating `IEI` reads low: device neither requests nor answers.
- **Byte link untimed.** Characters not paced by baud rate: linked channel transmits as fast as CPU writes, receives as fast as host supplies. Peer's RTS not read; only `NotCTS` pin gates linked transmitter. Linked channel cannot carry break or errors.
- **Dropped characters.** Character offered while receiver not active discarded, not held.

## Test coverage

`Z80SIO.devtest` covers:

- Reset state; pointer protocol (registers on both channels, WR2 in channel B only, RR1 and RR2 reads, pointer return); Channel Reset on each channel, incl. effect on services in progress.
- Transmitter: x1 8N1 to exact edge, buffering, back-to-back frames, disabled and re-enabled transmitter, disable during frame; 7, 6 and 5-or-fewer data bits (every count one to five); even and odd parity, with seven bits; x16 with parity and two stop bits to exact edge; 1½ stop bits; x64 bit length; Send Break; RTS held until All Sent; DTR; CTS auto enable; synchronous mode inert.
- Receiver: x1 and x16 8N1; mid-bit sampling in x16 with bits correct only in their middle; stop bit sample point; glitch rejection; back-to-back characters; x32 on channel B; disabled and re-enabled receiver; 5, 6, 7 bits with fill ones; parity in high bit, parity errors and their latch for both senses; eight bits with parity; framing error and wait for line to rise; break start and end; three-character FIFO, overrun, Error Reset; one read per strobe across clock edges; DCD auto enable.
- Interrupts: receive modes (disabled, first character with command 4, all characters, parity special or not, special-condition hold); all eight vector codes and WR2 handling; transmit request, blocked while in service, command 5, data write and enable clear; external/status latch, one request per change, acknowledge versus Reset External/Status, CTS, DCD, SYNC, break; Transmit Underrun latch; RR2; priority across all six sources, nesting, RETI and non-RETI byte sequences, command 7, `IEI` low, `IEO`.
- Wait/Ready: Ready on transmit and receive, Wait on transmit and receive, other channel's pin, disabled states.
- Hardware reset of busy device.

`Z80SIO_byte.devtest` runs device with every channel byte-linked in synchronous loopback (`device Z80SIO link=loopback`) and covers: character path and timing, independent channels, serial pins ignored, character length each way (8, 7, 6, 5 bits), host queue (seven characters in order through three-deep FIFO), transmitter, receiver and auto enables, synchronous-mode inertness, transmit and receive interrupts, RTS output.

Threaded `Terminal` path checked by hand with `sleep` (`link=echo`: ten characters echoed through null-modem-linked `Terminal`, read back in order; `link=tcp port=N`: TCP client connected to `TcpTerminal` null-modem linked to channel A, host's characters held back until CPU set RTS, then received, and CPU-written characters delivered to socket). Not in committed scripts.

Not exercised in committed scripts: `Terminal` adapter and RTS callback (hand-checked as above), blocking `HostReceive` under full queue, x32 transmit and x64 receive (one lookup table covers all clock modes, which other combinations exercise); receiver clock edge coincident with `RxD` change; exact edge on which transmit interrupt requested relative to simultaneous acknowledge.
