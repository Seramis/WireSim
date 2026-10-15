---
name: device-inspect
description: Retrofit one existing Sim::Device with the opt-in IRegisterInspectable/IMemoryInspectable interfaces, verified standalone with DeviceHarness -- no machine needed. Use when a device needs to be readable by MachineHarness or other tooling after the sim is stopped, e.g. before using it in a machine built with machine-engineer.
---
# Making a device inspectable

Dis skill add `Sim::IRegisterInspectable` and/or `Sim::IMemoryInspectable` (`src/Simulator/Inspect.hpp`) to **one existin device**. Change small, add-only: device get base class and one or two `const` method, pins/registers/behavior no change. Changin behavior be `device-cpp`/`device-verilog` job. Verify standalone thru `DeviceHarness`.

Read `src/Simulator/Inspect.hpp` for interface, and `src/Testing/DeviceHarness.md`'s "Register/memory inspection" for script verb dis unlock.

Call dis skill direct, or when `machine-engineer` find un-inspectable component and user agree. Never run auto as side effect of buildin machine.

## 1. Choose the interface

- Register-holdin device, typically CPU, implement `IRegisterInspectable`. `DumpRegisters()` return `vector<pair<string, uint64_t>>` of every register worth showin.
- Byte-addressable storage device (RAM, ROM) implement `IMemoryInspectable`: `InspectableSize()`, `Peek(address)`, `Poke(address, value)`.
- Device rare be both. Most be neither.

## 2. Implement

```cpp
class MyChip final : public Sim::Device, public Sim::IRegisterInspectable {
public:
    // ... existing Pins, constructor, Logic() unchanged ...

    std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
        return {{"A", _a}, {"X", _x}, {"Y", _y}, {"PC", _pc}};
    }

private:
    // ... existing fields ...
};
```

`Peek`/`Poke` read and write device existin backin storage direct. Dey skip pins and `Logic()`, so safe any time, even before `Platform::Start()` (dis how `MachineHarness`'s `Boot()` and `poke` use dem).

Neither interface ever called from `Logic()` or other hot path, so ordinary virtual dispatch fine even tho `Device::Logic` avoid vtable.

## 3. Verify with DeviceHarness

Register device if not already (`device-harness` skill), den have `.harness.cpp` shim set capability pointer, like `h.Ui` for UI device:

```cpp
h.Registers = dynamic_cast<Sim::IRegisterInspectable*>(dev.get());
h.Memory = dynamic_cast<Sim::IMemoryInspectable*>(dev.get());
```

Skip line dat no apply; both default null. Den exercise interface:

```
device MyChip
dumpregs
expect reg.A = 0x00
poke 0x10 0x7F
expect mem[0x10] = 0x7F
```

`dumpregs`, `peek`, `poke`, `expect reg`, `expect mem` fail at parse time 'gainst device whose matchin pointer null.

## 4. Document

Add interface to device `<Name>.md`, in section coverin its state (usually "Internal state reference"). It just existin state becomin externally visible, so no need own section.

## 5. Commit

Add new line to device `.devtest` (or short new section if none exist) and re-run whole script. Commit like any other `.devtest` change.

## Why this is separate from machine-engineer

Machine component list use only device already exist and pass own `.devtest`, so `machine-engineer` never edit one. When it find CPU or memory device not inspectable, it stop and ask whether run dis skill first or go on without state assertion for dat component. Keepin dis separate skill mean device inspectability decide and verify on its own term, not as side effect of first machine dat need it.