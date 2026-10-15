---
name: device-verilog
description: Add, extend, modify, or debug a Verilog (HDL) hardware device in the WireSim netlist -- write/change the .v + .f manifest, verilate it via CMake, and wrap it as a first-class Sim::Device, verified standalone with DeviceHarness. Use when creating a new hardware chip in Verilog rather than C++, changing an existing one's ports/behavior, fixing one that misbehaves, or when a build is silently missing/dropping generated Verilator files.
---
# Verilog devices in WireSim

Skill cover lifecycle of Verilog-backed `Sim::Device`: add, extend, modify, debug one. Verilog devices first-class: expose `Sim::Pin`/`Sim::Bus` exactly like C++ device. `src/Devices/Register/Register8/` (8-bit latch) reference. Copy its shape, include folder-per-device layout (category / device name / that device's files).

All work happen through `DeviceHarness`, no CPU, ROM or machine. For combine devices into running computer, see `device-cpp`'s "Machines are a separate concern".

## Which case is this?

- **New device:** follow steps 1-3.
- **Extending or modifying:** new port, register, mode or behavior on existing chip. Each step got "Extending" note.
- **Debugging:** wrong value, timing or contention, or build silently missing generated Verilator files. Go to "Debugging a misbehaving device".

## 1. Understand the chip (or the change)

Follow `device-cpp` step 1: extract register/pin map and timing from datasheet (`pdftotext -layout docs/<chip>.pdf -`) or text description. Decide which registers and bits stateful or self-clearing, whether chip need cycle-accurate timing (often more natural in Verilog than C++) or can model at transaction level, and what deliberately not modeled. For chip with big register or mode surface, build and verify one module or mode at a time.

**Extending:** read device's `<Name>.md` first (Internal state reference, Limitations).

## 2. HDL and manifest, same directory

Write `<Name>.v` (or several files) and `<Name>.f`: one Verilog source path per line, relative to that directory; blank lines and `#` comments ignored. Manifest's filename without `.f` is top module name and generated-class prefix (`V<Name>`).

`CMakeLists.txt` find every `src/Devices/**/*.f` with `file(GLOB_RECURSE ...)` and call Verilator's `verilate()` CMake function (from `find_package(verilator)`) once per manifest. That build `<Name>_lib` static library with output in device's git-ignored `_gen/` subfolder.

**Use `verilate()`, not hand-written `add_custom_command` plus `add_library(... ${GENERATED_CPP})`.** Verilator 5.x split module's generated code across several `V<Name>___024root*.cpp` / `V<Name>__Syms*.cpp` files, set depend on design size. `verilate()` ask Verilator (`--make json`) for list. Custom command that track only `V<Name>.cpp` compile and link, then silently produce model missing rest of its class. Suspect this if Verilog device build but misbehave or fail to link mysteriously.

**Extending:** editing existing `.v` (no new files or ports) need only normal incremental build. Adding source file to manifest, or new `.f`, need re-configure to re-glob: `cmake --preset debug`.

## 3. Thin `Sim::Device` wrapper, same directory

Name after chip (e.g. `Register8.hpp`, class `Register8`).

- Own Verilated model with `std::unique_ptr<V<Name>>` (`std::make_unique<V<Name>>()`), not raw pointer with hand-written destructor.
- Expose chip's real pins as `Sim::Pin`/`Sim::Bus` members: CE/WE/OE plus `D` bus for anything memory-mapped, as `ParallelSram` do.
- In `Logic()`, copy bus-derived values onto model's input ports, call `eval()`, then drive output pin or bus from model's output ports. Tri-state `D` with `Bus::Clear()` when not selected, as `ParallelSram` and `ST16C550` do. Verilog model got no notion of tri-state; that live in C++ wrapper.
- Model's top-level ports are plain data members on generated `V<Name>` class (`VL_IN8`/`VL_OUT8` expand to `uint8_t`-like fields). Read and write them directly.
- `TriggerMask` got same three checks as C++: `mask.IsSet(bit)` (prefer device's own trigger enum, like `CTRL` below), `IsSetAny(bits...)`, `IsSetAll(bits...)`. See `device-cpp` step 3.
- Check mask first, before any pin or bus read, for block that is one-shot reaction to specific triggers (latch on edge, log transition). Never substitute mask check for reading select, enable or reset pin's current level.
- Constructor parameter beyond pin pair (mode, byte count, name) passed through script's `device ... key=value` tokens via `ArgsMap`/`ArgValue`, as for C++ device. Design wrapper constructor with this in mind.

`src/Devices/Register/Register8/Register8.hpp` is real example. Same shape, trimmed for generic chip:

```cpp
#pragma once
#include "_gen/VMyChip.h"
#include "../../../Simulator/Platform.hpp"
#include <memory>
#include <string>

class MyChip final : public Sim::Device {
public:
    struct MyChipPins {
        Sim::Bus D;
        Sim::Pin NotCE, NotWE, NotOE;
    };
    MyChipPins Pins;

private:
    enum class CTRL : uint8_t { NotCE = 0, NotOE = 1, NotWE = 2 };
    std::unique_ptr<VMyChip> _model;

public:
    MyChip(Sim::Platform* p, std::string name = "MyChip")
        : Pins{ Sim::Bus::New(p, 8, name + ".D"),
                Sim::Pin::New(p, name + ".NotCE"), Sim::Pin::New(p, name + ".NotWE"), Sim::Pin::New(p, name + ".NotOE") },
          _model(std::make_unique<VMyChip>())
    {
        Pins.NotCE.AddTrigger(this, CTRL::NotCE);
        Pins.NotOE.AddTrigger(this, CTRL::NotOE);
        Pins.NotWE.AddTrigger(this, CTRL::NotWE, false);
    }

    // Not virtual, no `override`; see device-cpp's skeleton for why.
    void Logic(Sim::TriggerMask mask) {
        const auto isEnabled = Pins.NotCE.IsLow();
        if (!isEnabled) {
            Pins.D.Clear();
            return;
        }
        const auto isWrite = Pins.NotWE.IsLow();
        const auto isRead = Pins.NotOE.IsLow() && !isWrite;

        _model->we = isWrite ? 1 : 0;
        _model->d_in = isWrite ? Pins.D.Read<uint8_t>() : 0;
        _model->eval();

        if (isRead) Pins.D.Write(_model->d_out);
        else Pins.D.Clear(); // High-Z
    }
};
```

**Extending:** new port on `.v` module take effect only once wrapper's `Pins` struct and `Logic()` both add matching field. Module compile and link either way, so port added on only one side silently stay unconnected or stuck at reset value. Change both in same edit.

## 4. Document, then verify with DeviceHarness

From here identical to C++ device (`device-cpp` steps 4-5): short colocated `<Name>.md` (updated in place for extension), then `DeviceHarness` registration and `.devtest` (see `device-harness`), extended with new focused section for extension. If module's ports match bus width, its pins link straight to script's stubs with no bridging device.

Standalone harness verification matter more here. `Register8.v` is level-sensitive latch with no `clk` port, so repo got no example of convention a *clocked* Verilog model need (toggle `clk` field and call `eval()` on each edge). For chip with real clock input, work out that convention against `DeviceHarness` script first. Drive `CLK` like any pin (`drive CLK = H` / `step` / `drive CLK = L` / `step`) and confirm wrapper's `eval()` calls track it.

## Debugging a misbehaving device

Follow `device-cpp`'s "Debugging a misbehaving device" loop. Two points specific to Verilog:

- **Build silently missing generated files.** If device build but behave like stale or partial generated code (symptoms resemble missing method, or behavior not match current `.v`), check manifest's target use `verilate()` and not hand-written custom command (see step 2).
- **No internal-signal visibility by default.** `CMakeLists.txt`'s `VERILATOR_ARGS` (`-Wno-fatal -Wno-lint -Wno-style`) not pass `--public`, so only top-level ports reachable from wrapper. To inspect internal state, temporarily add debug output port to `.v` module and wire it in wrapper, rather than edit global `VERILATOR_ARGS`, which would affect every Verilog device and need full re-configure. Remove port before committing fix.

For long session, hand off to `.claude/agents/peripheral-device-debugger.md`.