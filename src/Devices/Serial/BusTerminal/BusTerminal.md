# BusTerminal

BusTerminal fictional byte-wide terminal interface, not model of real chip. Minimal character I/O device for building new machines: console needing no UART register model wired up first. Mixes in `Terminal` (`Serial/Terminal.hpp`), so connects to `TcpTerminal` or any other `Terminal`-based peer.

## Pinout reference

| Pin | Direction | Description |
| --- | --- | --- |
| `D` | bidirectional, 8-bit | Data bus. |
| `NotCE` | input | Chip enable, active low. Gates both `NotOE` and `NotWE`. |
| `NotOE` | input | Output enable, active low. Strobe (assert, then deassert) to advance to next received character. |
| `NotWE` | input | Write enable, active low. Strobe to send character currently on `D`. |

## Behavior

### Read

While `NotCE` and `NotOE` both low, `D` carries single buffered received byte, or `0x00` if none arrived. Each time `NotOE` newly asserts (falling edge while `NotCE` reads low), device pops buffer before driving it. First assertion already shows buffered character, following strobe (deassert, then reassert `NotOE`) shows whatever arrived since. `D` released (high-Z) whenever `NotCE` or `NotOE` high. Pop is level check re-evaluated on every `Logic()` call, so correct whichever of `NotCE` and `NotOE` reaches asserted level first.

### Write

While `NotCE` and `NotWE` both low, device captures what `D` carries. Releasing `NotWE` (rising edge) sends captured byte to any connected `Terminal` listener, no CTS-style gating this direction. One byte sent per `NotWE` strobe, mirroring read side's one character per strobe.

Write-side capture runs only at instant `NotWE` transitions (capture on assert, commit on release, same split SRAM write cycle uses). `NotCE` must already read low by time `NotWE` falls. Simultaneous same cycle fine. `NotWE` falling first while `NotCE` still high not fine: transition missed, eventual send captures stale data.

### Flow control (receive side)

Receive buffer one byte, kept honest by RTS/CTS flow control on `Terminal` side instead of queueing. Whenever buffer holds unread byte, device calls `Terminal::UpdateRTS(false)`. Through `Terminal::NullModemConnect` (or any other `AddRTS` listener), tells connected peer stop sending; `TcpTerminal` pauses socket reads, leaves bytes in kernel's TCP receive buffer instead of dropping them. `UpdateRTS(true)` goes out moment buffer read (`NotOE` pop).

Device also recomputes, re-announces RTS on every `Logic()` call, whether or not fullness changed. Has no register write or reset pin that could guarantee initial announce once peer connects, so next bus access (any `NotCE`/`NotOE`/`NotWE` change) serves as announce. Cheap because `Logic()` runs only on actual pin change, never free-running clock.

Byte arriving while buffer full (sender not honored RTS yet, or none connected) dropped instead of overwriting buffered one.

Write direction has no gating. Device not override `Terminal::CTS()`, so send always reaches connected peer's `AddTX` listener whatever peer's readiness.

## Limitations

- No reset pin, no register or mode surface beyond single output latch, single-byte receive buffer, write-capture register.
- No flow control on write direction.
- Asserting `NotOE` and `NotWE` together (with `NotCE` low) undefined on real SRAM-style chip, not special-cased. Device drives `D` for read while bus master drives it for write, shows up in `DeviceHarness`'s contention detection.

## Test coverage

`BusTerminal.devtest` covers `D` releasing to high-Z whenever `NotCE` high; read with empty receive buffer returning `0x00` across repeated `NotOE` strobes; `D` releasing again once `NotOE` or `NotCE` deasserts; write strobe (`NotCE`, `D` and `NotWE` asserting same cycle) completing no contention.

Does not cover byte arriving in receive buffer, RTS announcements, or written byte reaching connected `Terminal` listener. `DeviceHarness` single-device, `Receive()`, `Send()` and `UpdateRTS()` take effect only through connected peer `Terminal`. Verified these with throwaway program linking `Sim::Platform` directly (see `DeviceHarness.md`, "Verifying real-time or background-thread behavior"), using stub `Pin`/`Bus` objects `Platform::Link`-ed to device:

- `Receive('A')` followed by `NotOE` strobe reads byte back, second `Receive()` while buffer full dropped.
- `AddRTS` listener sees `UpdateRTS` go false as soon as `Receive()` fills buffer, true again once strobe empties it.
- `AddTX` listener sees `NotWE` strobe of `D = 'X'` deliver `0x58`, including single-cycle `NotCE` + `D` + `NotWE` ordering, whatever CTS state.