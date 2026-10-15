# LedSwitchBox

8-bit port, can read and write. Write latch onto 8 LEDs; read drive `D` from 8 switches. SDL2 window show both strips, hex readout each byte.

## Pins

| Pin | Direction | Notes |
| --- | --- | --- |
| `D` | in/out | 8-bit data bus |
| `NotCE` | in | chip select |
| `NotWE` | in | write strobe; latch `D` onto LEDs |
| `NotOE` | in | output enable; drive `D` from switches |

State two plain bytes (`_leds`, `_switches`). No Verilog model.

## Window and threading

Render thread start with no window, make one when `OpenWindow()` called. Device own SDL init/quit refcount, mutex, render thread, window. Only shared piece `SdlEventHub` (`../../Support/SdlEventHub.hpp`): render thread poll its window's events through it instead of call `SDL_PollEvent`, cause SDL event queue process-global and other window's thread could steal and drop this window's clicks (see `SdlEventHub.md`).

`_leds` and `_switches` are `std::atomic<uint8_t>` with relaxed access, not mutex-guard. Each byte one writer (`Logic()` for `_leds`, render thread's mouse handling for `_switches`) and one reader other side, so load never see torn value.

## Switches are UI state

Click switch flip bit in `_switches` from render thread. Never touch Pin, Bus or net; `Logic()` only read byte. Sit outside `Platform::Enqueue()` rule for external input into sim, cause no sim-side input to enqueue.

## Hex digits without a font

Both readouts two 7-segment digit (`DrawDigit` in `LedSwitchBox.cpp`) drive by segment table (`kSegments[16]`). Only hex nibble need, so no bitmap font use.

## Triggers

`NotCE` and `NotOE` immediate (combinational read and select response). `NotWE` stable, so debug print fire once per write instead once per immediate settle iteration. Latch `_leds` idempotent, so run unconditional in `Logic()`; only print gate on `mask.IsSet(CTRL::NotWE)`.