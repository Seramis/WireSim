# Verilog devices

Verilog-backed `Sim::Device`s first-class: expose `Sim::Pin`/`Sim::Bus` exactly like C++ device, and steps 1, 4, 5, 6 and debugging loop of [SKILL.md](SKILL.md) apply unchanged. This file covers what differs. `src/Devices/Register/Register8/` (8-bit latch) is reference; copy its shape, including folder-per-device layout (category / device name / that device's files).

## Step 1 additions

Chip needing cycle-accurate timing often more natural in Verilog than C++. Chip with big register or mode surface: build and verify one module or mode at a time.

## Step 2: HDL and manifest, same directory

Write `<Name>.v` (or several files) and `<Name>.f`: one Verilog source path per line, relative to that directory; blank lines and `#` comments ignored. Manifest filename without `.f` is top module name and generated-class prefix (`V<Name>`).

`CMakeLists.txt` finds every `src/Devices/**/*.f` with `file(GLOB_RECURSE ...)` and calls Verilator's `verilate()` CMake function (from `find_package(verilator)`) once per manifest. Builds `<Name>_lib` static library, output in device's git-ignored `_gen/` subfolder.

**Use `verilate()`, not hand-written `add_custom_command` plus `add_library(... ${GENERATED_CPP})`.** Verilator 5.x splits module's generated code across several `V<Name>___024root*.cpp` / `V<Name>__Syms*.cpp` files, depending on design size. `verilate()` asks Verilator (`--make json`) for list. Custom command tracking only `V<Name>.cpp` compiles and links, then silently produces model missing rest of its class. Suspect this if Verilog device builds but misbehaves or fails to link mysteriously.

**Extending:** editing existing `.v` (no new files or ports) needs only normal incremental build. Adding source file to manifest, or new `.f`, needs re-configure to re-glob: `cmake --preset debug`.

## Step 3: thin `Sim::Device` wrapper, same directory

Name after chip (e.g. `Register8.hpp`, class `Register8`).

- Own Verilated model with `std::unique_ptr<V<Name>>` (`std::make_unique<V<Name>>()`), not raw pointer with hand-written destructor.
- Expose chip's real pins as `Sim::Pin`/`Sim::Bus` members: CE/WE/OE plus `D` bus for anything memory-mapped, as `ParallelSram` does.
- In `Logic()`, copy bus-derived values onto model's input ports, call `eval()`, then drive output pins or bus from model's output ports. Tri-state `D` with `Bus::Clear()` when not selected, as `ParallelSram` and `ST16C550` do. Verilog model has no notion of tri-state; that lives in C++ wrapper.
- Model's top-level ports are plain data members on generated `V<Name>` class (`VL_IN8`/`VL_OUT8` expand to `uint8_t`-like fields). Read and write directly.
- `TriggerMask` has same three checks as C++: `mask.IsSet(bit)` (prefer device's own trigger enum, like `CTRL` below), `IsSetAny(bits...)`, `IsSetAll(bits...)`. See SKILL.md step 3.
- Check mask first, before any pin or bus read, for block that is one-shot reaction to specific triggers (latch on edge, log transition). Never substitute mask check for reading select, enable or reset pin's current level.
- Constructor parameters beyond pin pair (mode, byte count, name) pass through script's `device ... key=value` tokens via `ArgsMap`/`ArgValue`, as for C++ device. Design wrapper constructor with this in mind.

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

    // Not virtual, no `override`; see SKILL.md's skeleton for why.
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

**Extending:** new port on `.v` module takes effect only once wrapper's `Pins` struct and `Logic()` both add matching field. Module compiles and links either way, so port added on one side only silently stays unconnected or stuck at reset value. Change both in same edit.

## Steps 5-6: document and verify

Identical to C++ device (SKILL.md steps 5-6): short colocated `<Name>.md` (updated in place for extension), then `DeviceHarness` registration and `.devtest`, extended with new focused section for extension. Module's ports match bus width: pins link straight to script's stubs, no bridging device.

Use script macros for repeated clock-edge and register-access sequences (e.g. `CLOCK_EDGE` or `WRITE_REG addr value` macro), so each test section is short list of operations.

Standalone harness verification matters more here. `Register8.v` is level-sensitive latch with no `clk` port, so repo has no example of convention *clocked* Verilog model needs (toggle `clk` field and call `eval()` on each edge). Chip with real clock input: work out that convention against `DeviceHarness` script first. Drive `CLK` like any pin (`drive CLK = H` / `step` / `drive CLK = L` / `step`) and confirm wrapper's `eval()` calls track it.

## Debugging

Follow SKILL.md's "Debugging a misbehaving device" loop. Two Verilog-specific points:

- **Build silently missing generated files.** Device builds but behaves like stale or partial generated code (symptoms resemble missing method, or behavior not matching current `.v`): check manifest's target uses `verilate()`, not hand-written custom command (see step 2).
- **No internal-signal visibility by default.** `CMakeLists.txt`'s `VERILATOR_ARGS` (`-Wno-fatal -Wno-lint -Wno-style`) doesn't pass `--public`, so only top-level ports reachable from wrapper. To inspect internal state, temporarily add debug output port to `.v` module and wire it in wrapper, rather than editing global `VERILATOR_ARGS`, which would affect every Verilog device and need full re-configure. Remove port before committing fix.
