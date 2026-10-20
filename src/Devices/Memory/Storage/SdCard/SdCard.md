# SdCard

SD / SDHC memory card in SPI mode, backed by a file on the host. Block data is read from and written to the file on demand, so a card of any size costs no memory. The card is block-addressed (SDHC style): command arguments are 512-byte block numbers.

## Pinout reference

| Pin | Direction | Notes |
| --- | --- | --- |
| `NotCS` | in | Chip select. Low selects the card; high (or floating) deselects it and aborts any transfer in progress. |
| `SCLK` | in | Serial clock. SPI mode 0: `MOSI` is sampled on the rising edge, `MISO` changes on the falling edge. |
| `MOSI` | in | Data from the host, MSB first. |
| `MISO` | out | Data to the host, MSB first. Driven while selected (high when the card has nothing to say), released (`F`) while deselected. |

The socket's card-detect switch and write-protect tab are not part of the card and are not modeled.

## Construction

`SdCard(Sim::Platform*, const std::string& path, uint64_t sizeBytes)`.

- `sizeBytes` is the capacity, a non-zero multiple of 512, else `std::invalid_argument`.
- A missing `path` is created zero-filled at `sizeBytes`. A shorter file is extended with zeros. A longer file is rejected with `std::invalid_argument`, never truncated.
- The file stays open for the life of the device. Every written block is flushed to the file before the card answers.

## Behavior

### Bit and byte level

The host selects the card, then clocks bytes. Every byte clocked in is also a byte clocked out: the card shifts out queued response data, or `0xFF` when it has nothing to send. `MISO` is written from the stable phase, so it becomes visible one cycle after the `SCLK` falling edge; a host sampling on the rising edge is unaffected. Clock edges while deselected are ignored. Deselecting drops a partly received command, any queued response and any multi-block transfer. It keeps the card state (ready, CRC mode, pending `CMD55`).

### Commands

A command is six bytes: `0x40 | index`, a 32-bit argument (MSB first), CRC7 with the end bit. A response follows after one `0xFF` byte. R1 bits: `0x01` idle, `0x04` illegal command, `0x08` command CRC error, `0x40` parameter error.

| Command | Response |
| --- | --- |
| `CMD0` | R1 `0x01`. Returns the card to idle and turns CRC checking off. |
| `CMD8` | R1, then the voltage-range nibble and check pattern of the argument echoed (R7). |
| `CMD9`, `CMD10` | R1, token `0xFE`, 16-byte CSD (version 2.0) or CID, CRC16. |
| `CMD12` | Ends a multi-block read: R1 `0x00`, two busy (`0x00`) bytes. Illegal outside a multi-block read. |
| `CMD13` | R2: R1 and `0x00`. |
| `CMD16` | R1; the argument is ignored (fixed 512-byte blocks). |
| `CMD17` | R1, token `0xFE`, 512 data bytes, CRC16. |
| `CMD18` | As `CMD17`, then block after block until `CMD12`. Running off the end sends error token `0x08`. |
| `CMD24` | R1, then the host sends token `0xFE`, 512 data bytes and 2 CRC bytes. The card answers data response `0x05` and two busy bytes. |
| `CMD25` | As `CMD24` with token `0xFC` per block; the host ends with token `0xFD`, followed by two busy bytes. |
| `CMD55` | R1; the next command is an application command. |
| `ACMD41` | R1 `0x01` on the first poll after `CMD0`, `0x00` (ready) from the second on. |
| `CMD58` | R1, then the OCR: `0x00FF8000` while idle, `0xC0FF8000` once ready (power-up complete, high capacity). |
| `CMD59` | R1; argument bit 0 turns CRC checking on or off. |

`CMD17`, `CMD18`, `CMD24` and `CMD25` answer illegal command (`0x05`) until `ACMD41` has completed, and parameter error (`0x40`) when the block number is past the end. Every other command, and `ACMD41` without a preceding `CMD55`, answers illegal command.

### CRC

Read data CRC16 (CCITT, init 0) and the CSD/CID CRC7 are always computed correctly. Command CRC7 is checked for `CMD0` and `CMD8` always, and for every command after `CMD59` turned checking on; a mismatch answers R1 with the CRC-error bit and the command is not executed. Write data CRC16 is checked only with CRC checking on; a mismatch answers data response `0x0B` and nothing is written.

### CSD and capacity

The CSD is version 2.0, `C_SIZE = max(blocks / 1024, 1) - 1`. It reports capacity in 512 KiB units, so the block count is exact only for multiples of 512 KiB. The CID is a fixed value.

## Register inspection

Implements `Sim::IMemoryInspectable`: `Peek`/`Poke` address the card's bytes through the backing file, with no pins or timing involved. `InspectableSize` saturates at 4 GiB. A read transfer takes its data from the file when the block is queued, so poke before the command, not during a transfer.

Implements `Sim::IRegisterInspectable`:

| Name | Meaning |
| --- | --- |
| `Ready` | 1 once `ACMD41` completed, 0 after `CMD0`. |
| `CrcOn` | 1 while CRC checking is on. |
| `LastCmd` | Index of the last command received (`0xFF` before any). |
| `LastArg` | Its argument. |
| `LastR1` | Last R1 sent. |
| `Blocks` | Capacity in 512-byte blocks. |

## Limitations

- Always high capacity: block addressing and the SDHC OCR, whatever the size. Byte-addressed (SDSC) cards are not modeled.
- The card starts in SPI mode, ready for `CMD0`. The 74 initialization clocks and the native SD mode are not modeled, and the clock rate is not checked.
- No access, busy or programming time beyond two fixed busy bytes after a write.
- Not supported (answer illegal command): erase, write protect, lock, `CMD6`, `ACMD13`, `ACMD22`, `ACMD23`, `ACMD51`, and `CMD1`. `ACMD41` ignores the argument and `CMD16` the block length.
- A command received during a multi-block read ends the stream. Only `CMD12` is the defined way to do that.
- A failed write to the backing file answers data response `0x0D`; a failed read answers error token `0x01`.

## DeviceHarness

`device SdCard path=<file> size=<bytes> [fresh=1]`. `fresh=1` deletes the file first, so the card starts zero-filled; committed scripts use it with a path relative to the working directory (`sdcard-test.img`, left in the build directory).

`SdCard.devtest` drives SPI bit by bit: `BIT`/`CLK` macros clock one bit (MISO sampled at the rising edge), `TXB`/`RXB` send or check one byte, `BYTES1`/`BYTES8`/`BYTES64`/`SKIP_*` skip bulk bytes. It covers:

- deselected bus release and ignored clocks;
- the init sequence `CMD0`, `CMD8`, `CMD55` + `ACMD41`, `CMD58`;
- `CMD9` (CSD for a 64 MiB card) and `CMD10`;
- single block read, with CRC16 checked over the whole block, first and last block, and past the end;
- multi-block read with `CMD12`, and `CMD12` outside a stream;
- single and multi-block write, verified through `expect mem[...]` in the backing file, and write past the end;
- `/CS` aborting a transfer;
- CRC mode on and off: bad command CRC, bad and good write data CRC;
- `CMD0` resetting the card, bad CRC on `CMD0`, illegal commands while idle.

Not covered: a backing file shorter or longer than `size` (constructor errors), failed file I/O, and the CMD18 error token at the card's end.
