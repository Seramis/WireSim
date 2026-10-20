# AGENTS.md: Z80Retro

> **Scope:** machine facts for Z80Retro only. Repo-level [../../../AGENTS.md](../../../AGENTS.md) covers sim core, device rules, workflow rules; read first.

John Winans' Z80-Retro (2063-Z80 board, rev 4.3.x): Z80, 128 KiB SST39SF010 flash, 512 KiB AS6C4008 SRAM in sixteen 32 KiB banks, Z80 CTC, Z80 SIO (both channels bridged to TCP consoles), SD card socket (SPI bit-banged through the GP ports), glue logic. The machine has one firmware variant, `cpm`: the vendored [2063-Z80-cpm](vendor/2063-Z80-cpm) boot flash and BIOS (z80asm) boot CP/M 2.2 from an SD card image (cpmtools). [docs/](docs/) is the board manual (clone of Z80-Retro-Manual); the schematic is `docs/assets/2063-Z80.pdf`. Not modeled: SD card detect switch (GP input bit 6 stays pulled up), printer port, RS232 line levels, VDP and programmer daughter boards, the 1.8432 MHz baud clock.

## Build & run

Toolchain: repo level plus `z80asm` and `cpmtools` (`mkfs.cpm`, `cpmcp`; both packaged for Fedora, in the dev container) and the `vendor/2063-Z80-cpm` submodule (`git submodule update --init --recursive`, John Winans' boot flash, BIOS and the DRI CP/M 2.2 files). Without them CMake warns, builds no Z80Retro firmware.

```bash
cmake --build --preset release --target run_Z80Retro_cpm           # build, boot CP/M, keep stdin open (also "CMake: Build Target" in VS Code)
src/Machines/Z80Retro/scripts/build-firmware.sh -o DIR             # prints DIR/rom/Z80Retro (z80retro.bin and sdcard.img)
```

`scripts/build-firmware.sh` copies the needed vendor sources to a private work dir and calls `Firmware/build.sh` (z80asm, cpmtools). `DIR/obj/` holds the listings (`firmware.lst`, `retro.lst`) and symbols.

`Z80Retro` is its own binary. CMake assembles the firmware to `build/<preset>/rom/Z80Retro-cpm` (the default `--variant`). `--rom DIR` loads hand-built directory: `z80retro.bin` goes into the flash, and a `sdcard.img` next to it, if there is one, is put in the SD socket (no file, no card: MISO floats high and the boot flash reports it can't read the card). The card is a plain file read and written in place, so what CP/M saves stays in `sdcard.img` until the firmware is rebuilt. The `MachineHarness` target does not depend on firmware: build `Z80Retro` (or `check`) first, or the harness boots a stale image.

```bash
cmake --build --preset release --target Z80Retro
mkfifo in.fifo; sleep 100000 > in.fifo &                          # hold stdin open; kill that sleep by its own PID when done
build/release/Z80Retro --cycles 100000000000 < in.fifo &
```

Keys on stdin: `s` status, `r` reset, `v` toggle verbose diagnostic (very slow, huge output; keep short), `q` quit. Output buffered when redirected to file: appears at exit.

**Consoles:** TCP port 20024 is SIO channel A (CON), 20025 channel B (AUX) (`socat - TCP:localhost:20024`). The host never waits for RTS, so bytes reach the SIO whatever the firmware does with WR5 bit 1 (the CP/M BIOS never sets it). Bulk paste safe (4000 random bytes echo back identical). A client ends a line with LF (a lone CR is dropped), which the CPU sees as CR; client DEL arrives as BS (`TcpTerminal`).

**CP/M 2.2:** connect to CON. The boot flash prints its banner and the partition table, loads the BIOS, which brings up `a>` after about 16 million CPU cycles (10 s of wall time on a release build). `dir`, `stat`, `type bios.asm` and `save 1 hello.com` all work; drive A is the only one (8 MiB, 8k blocks, the `z80-retro-8k-8m` format of `vendor/2063-Z80-cpm/filesystem/diskdefs`) and holds the files of `cpm-2.2/filesystem`. Look at the card from the host with `dd if=sdcard.img of=a.img bs=512 skip=2048` and `cpmls -f z80-retro-8k-8m a.img` (with that `diskdefs` in the current directory).

**MachineHarness:** registers `cpu`, `ram` (SRAM, index = 19-bit address: bank × `$8000` + offset; high half is bank 15, address + `$70000`), `flash` (index = address), `chipset` (`FlashMapped`, `GpOut`, `PrnData`), `ctc` and `sio` points, and `sd` (registers and the card's bytes, index = byte offset in `sdcard.img`) when `rom=<DIR>` has a `sdcard.img`. `machine Z80Retro rom=... user1=closed` fits jumper J9 (GP input bit 5 low).

`Z80Retro.machinetest` (`ctest --test-dir build/release -R machinetest.Z80Retro`, or the `check` target; about 12 s on a release build) boots the `cpm` firmware for 18 million cycles and checks the card's registers (initialized, the last read was the directory's last block), the BIOS zero page (`JP WBOOT`, BDOS entry), the CCP loaded from the card and the console's SIO setup. Passing exercises the shadow flash, the banked SRAM, the SD socket wiring and the SIO. Not covered by script (check by hand over TCP): console receive, echo, flow control, CTC and interrupts.

## Machine facts

- **Memory:** after reset the flash answers every memory read, at any address (it is chip-enabled by `/MREQ`, not by address; `A16` tied low, so only its lower 64 KiB shows). Writes always reach SRAM (`/CE` = `/MREQ`, `/WE` = `/WR`); SRAM `/OE` is off while the flash is mapped. Any I/O cycle in the `$70` window maps SRAM instead (`/OE` = `/RD`); only reset brings the flash back (cross-coupled NAND latch). Consequently firmware copies its code onto itself (read flash, write SRAM), then disables the flash from inside that code. `$8000-$FFFF` is always SRAM bank 15; `$0000-$7FFF` shows the bank in GP output bits 7-4 (bank 15 there aliases the high half). Reset vector `$0000`; stack grows down from `$FFFF`.
- **I/O ports:** `/IORQ` low, `/M1` high, A7 low; A6-A4 pick a 16-port window, A3-A0 and A15-A8 not decoded, so a window repeats through its 16 ports. `$00` GP input (74LS244), `$10` GP output (74HC374: bit 0 SD MOSI, 1 SD CLK, 2 SD SSEL, 3 printer strobe, 4-7 bank), `$20` printer data (74HC374), `$30` SIO (A1 control/data, A0 channel B/A), `$40` CTC (A1:A0 channel), `$70` flash disable. `$50`, `$60` and everything with A7 set select nothing. 
- **Latches:** the 374s clock on the rising edge of their select, so an IN in their window latches the bus too (pulled high when idle: `IN` at `$1F` selects bank 15). Power-up value is 0 in the model; reset does not clear them. The 244 has no `/RD`: it drives the bus through any I/O cycle in `$00-$0F`, OUT included.
- **GP input:** bit 0-4 printer status, 5 USER1 (J9), 6 SD detect, 7 SD MISO; all pulled up, so `$FF` (`$DF` with J9 fitted, `user1` harness option). Only MISO is driven, by the SD card while it is selected.
- **SD card:** `SdCard` on GP output bit 0 (MOSI), bit 1 (CLK) and bit 2 (/SS, active low), answering on GP input bit 7. Sized from `sdcard.img`, always high capacity. It is deselected, and MISO released, while bit 2 is high. The boot flash expects the layout `build-firmware.sh -c` produces: an MBR whose partition 1 starts at block `$800` and holds drive A (CP/M system tracks, then the files).
- **Buses:** every address and data line pulled up, as are `/MREQ`, `/IORQ`, `/RD`, `/WR`, `/WAIT`, `/NMI`, `/BUSREQ` and the shared `/INT`.
- **Clock/reset:** one shared `CLK`, one T-state per cycle (the real board's 10 MHz). Reset held low first 10 half-cycles, fans out to Z80, CTC, SIO and the flash latch. `/WAIT`, `/NMI`, `/BUSREQ` tied inactive.
- **Interrupts:** CTC and SIO `/INT` (open drain) share the Z80's `/INT` line. Daisy chain: CTC `IEI` high, CTC `IEO` to SIO `IEI`, so CTC has priority. The CP/M BIOS polls.
- **CTC:** `ClkTrg0` and `ClkTrg3` on `CLK`; `ClkTrg1` and `ClkTrg2` would take the baud clock, which is not modeled (pulled low). `ZcTo` outputs unconnected.
- **SIO:** both channels byte-linked to `TcpTerminal`s (CON, AUX), so serial clocks and `RxD` are unused. `CTS` and `DCD` tied low (RR0 reads `$6C` after reset), `SYNC` parked inactive. The terminals ignore `RTS` (always clear to send), as a host without hardware flow control does, and take CR as the machine's line end.
- **Flash:** `SST39SF010` filled with `z80retro.bin` through `Poke` in `LoadRom()`; up to 128 KiB (shorter padded `$FF`), the image is the 64 KiB window. Writes ignored (programming not modeled).
- **SRAM:** `ParallelSram` w/ 19 address bits stands in for AS6C4008; address bits 15-18 come from the chipset's bank gates.
- **Speed:** about half Z80Echo's half-cycle rate (SIO and CTC models run on every `CLK` edge).
