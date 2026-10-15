# My6502: Memory Map & Components

Quick reference for write assembly 'gainst this machine. Decode logic in [Chipset.hpp](Chipset.hpp) (`OnIo`). Firmware-side constants in [Firmware/constants.inc](Firmware/constants.inc) (`HW::*` scope).

## Address space

| Range | Device | Notes |
|---|---|---|
| `$0000–$00FF` | SRAM (zero page) | `ZEROPAGE` segment |
| `$0100–$01FF` | SRAM (stack) | `STACK` segment |
| `$0200–$BFFF` | SRAM | general RAM and ROM-image space (banks 0-2, `BANK0`/`BANK1`/`BANK2` segments). `$BFFD–$BFFF` hold debug-mode marker (`'M'`, `'O'`, mode byte), checked at boot. |
| `$C000–$C007` | **ST16C550** | UART, `HW::UART` |
| `$C008` | **Register8** | 8-bit transparent latch (Verilog), `HW::REG8` |
| `$C009` | **DebugWriter** | write-only debug sink, `HW::DBGWRITER` |
| `$C00A` | **MRDI-1** IR | instruction register (write-only), `HW::MRDI1 + MRDI1::REG::IR` |
| `$C00B` | **MRDI-1** DR | data/parameter register (write-only), `HW::MRDI1 + MRDI1::REG::DR` |
| `$C00C` | **LedSwitchBox** | 8-bit LED/switch I/O port (read/write), `HW::LEDSWITCHBOX` |
| `$C00D–$FFF9` | SRAM | kernel code (`CODE`/`KERNEL` at `$C100`), `RODATA` |
| `$FFFA–$FFFF` | SRAM | 6502 vectors (`VECTORS` segment): NMI, RESET, IRQ |

Anything no match UART, Register8, DebugWriter, MRDI-1, or LedSwitchBox fall through to single `ParallelSram` device (`isMemory` in `Chipset::OnIo`). `$C000–$FFFF` thus plain RAM, preloaded from ROM bank images — not true ROM — writable same as any other address.

`constants.inc` also define `HW::BANK::BANK0/BANK1/BANK1W/BANK2/BANK2W` at `$C010–$C014`, but `Chipset` got no decode for them yet (marked `TODO` in firmware). No rely on bank switching in assembly.

## Components (wired in My6502.hpp)

| Component | Class | Bus width | Key signals from Chipset |
|---|---|---|---|
| CPU | `W65C02` | A(16)/D(8) | `CLK`/`CLKout`, `RESB`, `RWB`, `IRQB` (NMI and RDY pulled up, unused) |
| Chipset | `Chipset` | — | address-decode glue; owns all `/CE`, `/WE`, `/OE` enables above |
| Main RAM | `ParallelSram` | A(16)/D(8) | `MemCEB`/`MemWEB`/`MemOEB` |
| UART | `ST16C550` | A0-A2 slice/D(8) | `UartIOR(B)`, `UartIOW(B)`, `UartCS0/1/2B`, `UartINT` → CPU `IRQB`, `UartRST` ← `RSTB` |
| Register8 | `Register8` (Verilog) | D(8) only | `Reg8CEB`/`Reg8WEB`/`Reg8OEB` |
| DebugWriter | `DebugWriter` | D(8) only | `DbgWriterCEB`/`DbgWriterWEB` (write-only, no OE) |
| MRDI-1 display | `Mrdi1` | A0 + D(8) | `Mrdi1CEB`/`Mrdi1WEB` (write-only, no OE) |
| LED/switchbox | `LedSwitchBox` | D(8) only | `LedSwitchBoxCEB`/`LedSwitchBoxWEB`/`LedSwitchBoxOEB` |

### UART (`$C000–$C007`)

Register offsets per `UART::REG`: `RHR/THR`=0, `IER/DLM`=1 (DLAB-dependent), `ISR/FCR`=2, `LCR`=3, `MCR`=4, `LSR`=5, `MSR`=6, `SCR`=7. Bit-field constants for IER, FCR, ISR, LCR, MCR, LSR, MSR live in `constants.inc`. UART's IRQ line go straight to CPU's `IRQB`.

### MRDI-1 (`$C00A` IR / `$C00B` DR)

A 320×200 1bpp framebuffer (8000 bytes), MSB first per byte. Only opcode `DRAW_PIXELS = $00` used from assembly. Write IR, then DR take addr-lo, addr-hi, and stream of packed pixel bytes (auto-incrementing, wrap at 8000). Both registers write-only; writing IR again mid-stream reset FSM. Window open from console with `g` key, not from assembly. Full protocol: [Mrdi1.md](../../Devices/Graphics/Mrdi1/Mrdi1.md).

### Register8 (`$C008`)

Plain 8-bit transparent latch at single address. Read return last written byte while `/OE` asserted.

### DebugWriter (`$C009`)

Write-only debug sink at single address, no read path. Each byte written append to `debug_writer.log` in working directory (truncated at startup), for ad-hoc tracing from assembly without UART console. See [DebugWriter.md](../../Devices/Debug/DebugWriter/DebugWriter.md).

### LedSwitchBox (`$C00C`)

8-bit read/write I/O port with `Register8`'s pin interface (`/CE`, `/WE`, `/OE`). Write latch onto 8 simulated LEDs; read return position of 8 simulated switches. Window open with `l` key; switches toggled with mouse in that window. See [LedSwitchBox.md](../../Devices/Panel/LedSwitchBox/LedSwitchBox.md).

## Misc

- Reset assert for first 10 clock half-cycles (`_resetCycles` in `My6502.hpp`), then release.
- ROM images load at boot: `main.bank0.bin` → `$0000`, `bank1` → `$4000`, `bank2` → `$8000`, `bank3` → `$C000`.
- UART bridge to TCP terminal on port `20021` (`TcpTerminal`), null-modem connect to UART device. That be serial console when test firmware.