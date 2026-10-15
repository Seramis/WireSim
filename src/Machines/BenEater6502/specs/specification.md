# Engineering Specification: Ben Eater 6502 Breadboard Computer (Microsoft BASIC Configuration)

## 1. System overview

Doc tell architecture, component list, memory map, wiring, firmware layout of Ben Eater breadboard 6502 computer. Machine run **Microsoft BASIC** over serial terminal, with HD44780 LCD and piezo speaker as BASIC-addressable peripherals.

**Ground truth:** `vendor/msbasic/` (git submodule, `https://github.com/beneater/msbasic`, build with `-D eater`) be Ben Eater port of Microsoft BASIC to this machine, win if differ from doc. Every address, register value, wiring decision below come from `vendor/msbasic/eater.cfg`, `bios.s`, `lcd.s`, `sound.s`, `wozmon.s`, `token.s`, `init.s` and `header.s`. No PS/2 keyboard; char input come from serial terminal through ACIA.

## 2. Component list

| Qty | Component | Description | `WireSim` device |
| --- | --- | --- | --- |
| 1 | **W65C02S** | CMOS 8-bit CPU | `src/Devices/Cpu/W65C02/` |
| 1 | **W65C22** (or W65C22S) | VIA (Versatile Interface Adapter) for parallel I/O, Timer 1 | `src/Devices/Io/W65C22/` |
| 1 | **W65C51** (or MOS 6551) | ACIA/UART (Async Communications Interface Adapter) for serial I/O | `src/Devices/Serial/W65C51N/` |
| 1 | **AT28C256** | 32 KB parallel EEPROM, firmware storage | `src/Devices/Memory/Rom/AT28C256/` |
| 1 | **AS6C62256** | 32 KB low-power SRAM | `src/Devices/Memory/Sram/ParallelSram/` |
| 1 | **16x2 character LCD** | HD44780-compatible module | `src/Devices/Graphics/Hd44780/` (`Hd44780Variant::Lcd1602`) |
| 1 | **Piezo speaker** | Drive by VIA PB7 Timer1 square-wave output, for BASIC `BEEP` statement (`vendor/msbasic/sound.s`) | no dedicated device; a `Pin`/probe on VIA `PB7` |
| 1 | **74HC00** (or 74HCT00) | Quad 2-input NAND gate (address decoding) | not device; be `Chipset` combinational decode (§3.1) |
| 1 | **MAX232** (or TTL-to-USB converter) | RS-232 level shifter / serial adapter | not modeled; `W65C51N` give byte-level host transport direct (§4.4) |
| 1 | **1 MHz oscillator** | Clock oscillator can (or 555 / logic clock module) | not modeled; `Machine` toggle shared `Phi2` pin once per sim half-cycle (§4.1) |
| 3-4 | Solderless breadboards | Circuit mounting | physical build only |
| — | Passives and hardware | 22 AWG hook-up wire, 0.1 µF decoupling capacitors, 10 kΩ potentiometer (LCD contrast), 220 Ω resistors | physical build only; no sim effect |

Power, ground pins of every IC should carry 0.1 µF ceramic decoupling capacitor close to supply pins, suppress noise, transient reset glitch on breadboard rail. This have no sim counterpart.

## 3. Memory map

Take from `vendor/msbasic/eater.cfg` `MEMORY` and `SEGMENTS` block, register constant in `bios.s`, `lcd.s`, `sound.s`.

| Address range | Size | Component | Description |
| --- | --- | --- | --- |
| `$0000–$3FFF` | 16,384 B | **AS6C62256 SRAM** | Zero page (`$0000–$00FF`), hardware stack (`$0100–$01FF`), msbasic IRQ-fed serial ring buffer (`$0300–$03FF`, `INPUT_BUFFER` segment), BASIC program, variable storage from `$0400` (`RAMSTART2`). Only lower half of chip reach (A14 low). |
| `$4000–$4FFF` | 4,096 B | unmapped | Nothing drive bus. msbasic `MEMORY SIZE?` probe stop at `$4000` (`15359 BYTES FREE`) before write to ACIA. |
| `$5000–$500F` | 16 B | **W65C51 ACIA** | Only `$5000–$5003` (`ACIA_DATA`/`STATUS`/`CMD`/`CTRL`) use. 16-byte decode window be doc convention, not msbasic requirement. |
| `$5010–$5FFF` | 4,080 B | unmapped | |
| `$6000–$600F` | 16 B | **W65C22 VIA** | `PORTB`/`PORTA`/`DDRB`/`DDRA`/`T1CL`/`T1CH`/`ACR` use (§4.2). `PCR`/`IFR`/`IER` exist at usual offset but firmware no touch them. |
| `$6010–$7FFF` | 8,176 B | unmapped | |
| `$8000–$FFFF` | 32 KB | **AT28C256 EEPROM** | Firmware ROM, contig, no carve-out (§5.2) |

### 3.1 Decode logic (replaces the 74HC00 NAND network)

`Chipset` decode every enable combinational from full 16-bit address bus. I/O window sit in lower (non-ROM) half, so `eater.cfg` `BASROM` segment span clean, uncarve `$8000–$FFFF`:

- `rom_ce  = (A15 == 1)` for `$8000–$FFFF`, no exception.
- `acia_ce = (A15 == 0) && (A[14:4] == 0b1010000000)` for `$5000–$500F` (`(addr & 0xFFF0) == 0x5000`).
- `via_ce  = (A15 == 0) && (A[14:4] == 0b1100000000)` for `$6000–$600F` (`(addr & 0xFFF0) == 0x6000`).
- `sram_ce = (A15 == 0) && (A14 == 0)` for `$0000–$3FFF`.
- No select for rest of `$4000–$7FFF`. Bus float (read 0 in sim).

`ParallelSram`, `AT28C256` each tie 15-bit `A` bus to system bus low 15 bit, whichever window access (as `AT28C256.hpp` `kAddressBits = 15` expect). `Chipset` never assert more than one of `sram_ce`, `acia_ce`, `via_ce`, `rom_ce` for address, so no read-side bus contention to model.

## 4. Hardware wiring

### 4.1 System bus and control

- **Data bus (D0–D7):** tie parallel across CPU, SRAM, EEPROM, VIA, ACIA.
- **Address bus (A0–A15):** all 16 bit go from CPU to `Chipset` decode logic (§3.1). Each device get only many low-order bit as own address bus wide (15 for SRAM, ROM; 4 for VIA, ACIA register select).
- **Clock (Phi2):** one shared `Phi2` pin, toggle once per sim half-cycle by `Machine`, fan out to CPU, every peripheral.
- **Reset (RESB):** one shared active-low `RESB` pin, hold low first **10 half-cycles** after `Machine` start, then release, fan out to CPU, every peripheral.
- **Interrupt (IRQB):** `vendor/msbasic/bios.s` `IRQ_HANDLER` service only ACIA. It read `ACIA_STATUS`/`ACIA_DATA` unconditional, note "assume only source of interrupt be incoming data". VIA `IER`/`PCR` never configure to assert own `IRQB`. Wire `ACIA.IRQB` direct to CPU `IRQB`. `VIA.IRQB` may share same open-drain node (harmless, firmware never assert it) but not require for BASIC/WozMon image run.

### 4.2 VIA wiring

Register base `$6000` (§3). Port B fully output after init (`DDRB = $FF`). Port A drive only bit 0 (`DDRA = $01`).

| VIA pin | Connection | Purpose |
| --- | --- | --- |
| **PB0–PB3** | LCD `DB4–DB7` | 4-bit nibble data. `lcd.s` `LCDINIT` switch HD44780 into 4-bit mode after initial 8-bit handshake. |
| **PB4** | LCD `RS` | Register select |
| **PB5** | LCD `RW` | Read/write |
| **PB6** | LCD `E` | Enable strobe |
| **PB7** | Piezo speaker | Timer1 square-wave output (`ACR = $C0` during `BEEP`, per `sound.s`) |
| **PA0** | External RTS line to host terminal (via MAX232, physical build only) | Software flow control. `bios.s` `CHRIN`/`IRQ_HANDLER` set it high when serial ring buffer ≥`$F0` full, clear once drain below `$B0`, independent of ACIA own (unused) hardware RTS/CTS pin. |
| **PA1–PA7** | Unused | Left float; not wire by firmware |

This be `vendor/msbasic/lcd.s` wiring.

### 4.3 HD44780 LCD wiring

Pin per `Hd44780.md`: `DB` (only `DB4–DB7` meaningful once 4-bit mode select), `RS`, `RW`, `E`. See §4.2 for VIA connection. `BL` (backlight) tie active; `vendor/msbasic/` have no firmware control of it.

### 4.4 ACIA serial wiring

Register base `$5000` (§3). `W65C51N` give byte-level host-terminal transport (`Devices/Serial/Terminal.hpp`) in place of real `RxD`/`TxD` pin, MAX232/USB-serial adapter. No bit-level serial line, baud-rate generator, crystal model (see `W65C51N.md`, "Known simplifications"). `ACIA.IRQB` go straight to CPU `IRQB` (§4.1).

## 5. Firmware

### 5.1 Register constants (from `vendor/msbasic/bios.s`, `lcd.s`, `sound.s`)

```
ACIA_DATA  = $5000   ACIA_STATUS = $5001   ACIA_CMD = $5002   ACIA_CTRL = $5003

VIA_PORTB  = $6000   VIA_PORTA   = $6001   VIA_DDRB = $6002   VIA_DDRA  = $6003
VIA_T1CL   = $6004   VIA_T1CH    = $6005   VIA_ACR  = $600B
```

`vendor/msbasic/` own `bios.s`, `lcd.s`, `wozmon.s` be write for this machine, use as-is through submodule, not copy.

### 5.2 EEPROM layout (`$8000–$FFFF`, from `eater.cfg`)

```
+---------------------------------------------------+ $8000
| HEADER: "jmp COLD_START" (3 bytes)                 |
+---------------------------------------------------+
| Microsoft BASIC core, I/O drivers (bios.s), LCD    |
| driver (lcd.s), BEEP (sound.s)        ($7E00 - 3B) |
+---------------------------------------------------+ $FE00
| WozMon monitor (vendor/msbasic's own wozmon.s)     |
+---------------------------------------------------+ $FFFA
| NMI vector | RESET vector | IRQ/BRK vector         |
+---------------------------------------------------+ $FFFF
```

It be produce by `ca65 -D eater msbasic.s -o eater.o && ld65 -C eater.cfg eater.o -o eater.bin`. `eater.bin` be exact 32 KB (`$8000–$FFFF`), load direct into `AT28C256` image, no gap to exclude.

### 5.3 Boot flow (from `wozmon.s`'s `RESET` and `header.s`'s `jmp COLD_START`)

1. **`RESET`** (vector at `$FFFC–$FFFD`, define in `vendor/msbasic/wozmon.s`): `CLD`, `JSR INIT_BUFFER` (set `VIA_DDRA = $01`, clear PA0 flow-control line), `CLI`, then configure `ACIA_CTRL = $1F` (8-N-1, 19200 bps), `ACIA_CMD = $89` (DTR enable, **receiver interrupt enable**). Then fall into WozMon command loop.
2. **Reset land in WozMon, not BASIC.** No automatic jump to BASIC cold start. User type `8000R` at WozMon `\` prompt to jump to `$8000` (`HEADER` segment `jmp COLD_START`), which run `init.s` cold-start sequence. That call `LCDINIT` (`lcd.s`) once, among other setup, enter BASIC REPL.
3. **LCD not automatic CHROUT mirror.** `CHROUT` (`bios.s`) write only `ACIA_DATA`. LCD drive only through two statement this port add, `LCDCMD` (raw instruction byte), `LCDPRINT` (print string or expression), both register in `token.s`. BASIC program must call them explicit.
4. **Serial input be IRQ-fed, buffer-poll.** `IRQ_HANDLER` (vector at `$FFFE–$FFFF`) read `ACIA_DATA` every IRQ, push into 256-byte RAM ring buffer (`INPUT_BUFFER` at `$0300`, separate from WozMon `$0200` line-edit buffer). `CHRIN` drain that ring buffer instead of read `ACIA_DATA` direct. `NMI` (vector at `$FFFA–$FFFB`) point to `$0F00`, RAM address with no code preload. Reserve for user-install handler (e.g. manual break switch), unused by default.

### 5.4 Interrupt policy

Firmware enable exact one interrupt source: ACIA receive-data-register-full interrupt (`ACIA_CMD = $89`). VIA `IER`/`PCR` stay at power-on-reset default (no interrupt enable), even though Timer1 use for `BEEP`. `BEEP` poll, delay in software, no wait on T1 interrupt (`sound.s`). `Chipset`, `Machine` need no interrupt-priority or OR logic beyond wire `ACIA.IRQB` to CPU (§4.1). VIA `IRQB` can leave unconnect or wire to same node, no behavior difference.

### 5.5 Build

`vendor/msbasic/make.sh` already build `eater` target (`ca65 -D eater msbasic.s -o tmp/eater.o && ld65 -C eater.cfg tmp/eater.o -o tmp/eater.bin`). This machine `Firmware/build.sh` (toolchain-specific half of firmware-build contract in `machine-engineer` skill) run same two command against submodule, copy `eater.bin` into ROM output directory as `AT28C256` image. No new assembly need: submodule use as-is, not port or copy into `Firmware/`.