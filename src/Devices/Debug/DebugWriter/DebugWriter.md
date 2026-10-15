# DebugWriter

Write-only debug sink. Each byte CPU write to it get appended to file or printed to stdout. Nothing can be read back.

## Pins

| Pin | Direction | Notes |
| --- | --- | --- |
| `D` | in only | 8-bit data bus; device never drive it |
| `NotCE` | in | chip select |
| `NotWE` | in | write strobe |

No `NotOE`, no output path.

## Output modes

Mode chosen at construction time.

- **`filePath` given:** file get opened truncated once, so every run start empty. Each write append one raw byte, flushed right away. Path relative to working dir simulator run from.
- **`filePath` omitted:** each write printed with `std::println` as `[DebugWriter:<name>] write <hex>`. This print IS device's real output, not trace, so unlike most device logging it **not** gated on `Diagnostics::debugMode`.

From 6502 assembly, write is `LDA #value` / `STA <address>`. Address wherever machine's `Chipset` decode device (see machine's memory map).

## Trigger choice

`NotWE` stable trigger, `NotCE` immediate. `Logic()` emit byte only when `mask.IsSet(CTRL::NotWE)`, which fire once per cycle. Without that check, immediate loop could emit same byte many times per cycle while it converge.