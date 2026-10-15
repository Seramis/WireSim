# MRDI-1: Monochrome Raster Display Interface

320x200 1-bit framebuffer controller (`Mrdi1`) with 40x25 char text layer, always sit in front of it. Addressed through two write-only register on 8-bit data bus:

| A0 | Register | Effect on write |
| --- | --- | --- |
| 0 | IR (instruction) | (Re)start instruction cycle: latch `data` as opcode, reset FSM to `ExpectAddrLow`. `0x00`, `0x01` and `0x02` (`DRAW_PIXELS`, `WRITE_TEXT`, `TEXT_ATTR`) defined. Any other value still reset FSM, but its stream bytes get dropped (see `BufferSizeFor`). |
| 1 | DR (data) | Feed FSM argument stream for current opcode. |

## Shared address and streaming protocol

All three opcode share one FSM, one auto-incrementing pointer (`_vramPtr`) and one `CTRL::NotWE` commit path. Only one stream active at a time, so no per-opcode pointer. After `IR <- opcode`, device expect on DR, in order:

1. Start offset low byte, then `ExpectAddrHigh`.
2. Start offset high byte: `vramPtr = (low | high<<8) % size`, then `Streaming`. `size` be `VramSize` (8000) for `DRAW_PIXELS`, or `TextSize` (1000 = 40*25) for `WRITE_TEXT` and `TEXT_ATTR`.
3. Any number of byte. Each byte get stored into opcode's buffer at `vramPtr`, which auto-increment and wrap at same `size`.

Writing IR again at any point abort current stream, restart FSM at `ExpectAddrLow` for new opcode.

### DRAW_PIXELS (`0x00`)

Packed pixel byte (MSB first within each byte) go into `_vram`: 320x200, 1 bit per pixel, 8000 byte.

### WRITE_TEXT (`0x01`)

One char code per byte into `_textVram` (1000 cell, linear `row*40 + col`). Code outside printable ASCII (`0x20`-`0x7E`) render as blank cell, pixel layer show through. All-zero default buffer therefore fully blank, same as `_vram`'s all-zero default be solid black.

### TEXT_ATTR (`0x02`)

One attribute byte per char cell into `_textAttr`, indexed like `_textVram`. Only bit 0 (`AttrInvert`) defined. Other bit reserved, stored unmasked. Default `0` be plain white text on transparent background.

## Text and pixel compositing

Render thread draw pixel layer first, then text layer cell by cell, using baked-in `Font8x8` table (8x8 cell). In both normal and inverted cell, only glyph's ink pixel get drawn. Background pixel never touched, so pixel layer stay visible through cell, all-zero text buffer invisible instead of black block. `AttrInvert` only recolor ink from white to black, never fill background. Inverted glyph over default black pixel background blend into it by design; `AttrInvert` for light pixel layer underneath.

## Font

`Font8x8` (anonymous namespace in `Mrdi1.cpp`) cover printable ASCII `0x20`-`0x7E`. Has one `uint8_t[8]` per glyph, one row per byte, MSB first (same bit convention as `DRAW_PIXELS`). Generated offline from `font.otf` ("Minecraft Standard") with FreeType at `FT_Set_Pixel_Sizes(0, 6)` and `FT_LOAD_TARGET_MONO`, then baseline-aligned into 8-row cell as `cellRow = 7 - bitmap_top + i`, where `i` be row index within rendered glyph.

Baseline row 7 (cell's last row) work for every glyph. Across printable range tallest ascent be `bitmap_top = 7`, nothing descend below baseline (font has no true descender; `g`, `p`, `q`, `y` and `j` sit flush with it), so `cellRow` stay within `[0, 7]` without clamp. Glyph left-align at column 0 (`bitmap_left = 0` for all). Widest glyph (`@`, `~`) be 6 px, so column 6-7 always blank, serve as inter-char spacing. No regen script checked in. To regen table, run same FreeType call (`FT_Load_Char` with `FT_LOAD_RENDER | FT_LOAD_TARGET_MONO`) against `font.otf` at size 6, apply mapping above.

## Why `NotWE` is the only trigger, and stable

`Logic()` register single **stable** trigger on `NotWE`. Stable trigger run only after immediate loop converge, so every pin read in `Logic()` be settled, never decode-lag transient (see `overview.md`, "Decode lag").

Matter because commit not idempotent: it advance `vramPtr` and FSM state. Committing same stream byte twice would silently eat or shift byte of pixel stream. Plain memory write, by contrast, land same byte at same address twice, invisible.

## Threading model

Each `Mrdi1` own render thread (`RenderLoop`) that start idle, no SDL2 window, open one when `OpenWindow()` called. `RenderLoop` be outer wait loop around window's create, pump and destroy cycle. It block on `_openCv` until `_openRequested` (set by `OpenWindow`) or `_stopRequested` (set by `~Mrdi1`), create window, run render and event loop until user close it or device destroyed, destroy window, wait again. Window therefore can get closed and reopened any number of time within thread's single `SdlAcquire`/`SdlRelease` lifetime. `OpenWindow()` be no-op if window already open (`_windowOpen`, atomic set only by `RenderLoop`).

None of this touch `Logic()` or `CommitWrite()`. VRAM write land in `_vram` same way whether window open, closed or reopening, FSM has no notion of window.

While its window open, render thread:

- Touch only its own `localVram`, `localText` and `localAttr` snapshot, copied from `_vram`, `_textVram` and `_textAttr` under `_vramMutex` when `_vramDirty` set. One dirty flag cover all three layer: write to any force full repaint of both layer, cheap at this resolution. Never touch Pin, Bus or net, so sit outside `Platform::Enqueue()` rule for external input into sim. `OpenWindow()` likewise touch no Pin, Bus or net, so can get called directly from console-input thread.
- Poll its window's event every iteration through `SdlEventHub::PollEventsFor`, not `SDL_PollEvent` or shared pump thread. Avoid stealing another window's event, avoid Wayland deadlock; see `../../Support/SdlEventHub.md`.
- Share SDL subsystem init and quit, refcounted across all `Mrdi1` instance (`SdlAcquire`/`SdlRelease` in anonymous namespace in `Mrdi1.cpp`), guarded by mutex, because SDL's own refcounting not safe against concurrent `Init`/`Quit` from several thread.

## Extending

New opcode go in `CommitWrite`'s `_opcode`/`_state` handling. If new opcode need immediate (non-stable) reaction, such as reading status byte, re-derive whether stable-only trigger still sufficient. Do not add immediate trigger without re-checking idempotency argument above.