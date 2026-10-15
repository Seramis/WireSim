#pragma once
#include "../Simulator/Inspect.hpp"
#include "../Simulator/Platform.hpp"
#include "../Support/SdlUiTestHooks.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// -----------------------------------------------------------------------------
// Device-independent data model for the DeviceHarness tool (see
// src/Testing/DeviceHarness.md). A device's own registration shim (colocated
// as <Name>.harness.cpp) only needs this header: construct the real device,
// build a DeviceHandle whose `points` mirror its Pins struct via MakeStub,
// and self-register with HARNESS_REGISTER_DEVICE.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

class HarnessError : public std::runtime_error {
  public:
	using std::runtime_error::runtime_error;
};

// A single configuration token from a `device ... key=value` script line.
// The script grammar only ever carries a literal string through; it is each
// device's own registration shim that decides how to interpret it -- as an
// integer, as a literal byte string, or (via a leading '@') as the path to a
// file whose bytes should be loaded. This keeps the grammar fixed while still
// letting a future device (e.g. a ROM chip) take arbitrarily rich
// constructor arguments without any change to the harness itself.
struct ArgValue {
	std::string raw;

	std::vector<uint8_t> LoadBytes() const {
		if (!raw.empty() && raw[0] == '@') {
			std::ifstream file(raw.substr(1), std::ios::binary);
			if (!file) throw HarnessError("cannot open file: " + raw.substr(1));
			return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}
		return std::vector<uint8_t>(raw.begin(), raw.end());
	}

	int AsInt(int defaultValue = 0) const {
		if (raw.empty()) return defaultValue;
		return std::stoi(raw, nullptr, 0); // base 0: autodetects 0x/0 prefixes
	}

	const std::string& AsString() const {
		return raw;
	}
};
using ArgsMap = std::map<std::string, ArgValue>;

// A named point a script can drive/clear/expect/dump. Always the *external*
// stub side created by MakeStub below and Linked to the device's own pin --
// never the device's own Pin/Bus directly -- so a script can act on any pin
// generically regardless of its real direction (see DeviceHarness.md).
using TestPoint = std::variant<Sim::Pin, Sim::Bus>;

inline TestPoint MakeStub(Sim::Platform& platform, Sim::Pin devicePin, const std::string& name) {
	Sim::Pin stub = Sim::Pin::New(&platform, name);
	platform.Link(stub, devicePin);
	return TestPoint(stub);
}

inline TestPoint MakeStub(Sim::Platform& platform, Sim::Bus deviceBus, const std::string& name) {
	Sim::Bus stub = Sim::Bus::New(&platform, deviceBus.Count(), name);
	platform.Link(stub, deviceBus);
	return TestPoint(stub);
}

// Type-erased ownership for a device under test. Sim::Device deliberately has
// no virtual destructor (see Device.hpp) -- it is never meant to be deleted
// through a base pointer -- so a DeviceHandle can't just hold a
// unique_ptr<Sim::Device>. OwnedDevice<T> keeps the device alive behind a
// destructor that *is* virtual, scoped to this harness's own bookkeeping.
struct IOwnedDevice {
	virtual ~IOwnedDevice() = default;
};
template <typename T> struct OwnedDevice final : IOwnedDevice {
	std::unique_ptr<T> Value;
	explicit OwnedDevice(std::unique_ptr<T> value) : Value(std::move(value)) {}
};

// What one `device <alias> = <Type> ...` line produces: the device itself
// (kept alive via Owner) plus its named test points, in declaration order
// (so `dump` reads back in the same order as the device's own Pins struct).
struct DeviceHandle {
	std::string Type;
	std::unique_ptr<IOwnedDevice> Owner;
	std::vector<std::pair<std::string, TestPoint>> Points;
	// Non-owning; set by the registration shim (`h.Ui = dev.get();`) for a
	// device that inherits SdlUiTestHooks, left null otherwise. Backs the
	// `click`/`screenshot` script verbs -- see SdlUiTestHooks.md.
	SdlUiTestHooks* Ui = nullptr;
	// Non-owning; set by the registration shim via dynamic_cast (same
	// convention as Ui above) for a device that implements
	// Sim::IRegisterInspectable/IMemoryInspectable, left null otherwise.
	// Backs the `dumpregs`/`peek`/`poke`/`expect reg`/`expect mem` verbs --
	// see DeviceHarness.md's "Register/memory inspection".
	Sim::IRegisterInspectable* Registers = nullptr;
	Sim::IMemoryInspectable* Memory = nullptr;

	TestPoint* Find(const std::string& name) {
		for (auto& [n, point] : Points)
			if (n == name) return &point;
		return nullptr;
	}
};

using DeviceFactory = std::function<DeviceHandle(Sim::Platform&, const ArgsMap&)>;

// Process-wide map of device type name -> factory, populated by each
// device's HARNESS_REGISTER_DEVICE self-registration before main() runs.
class DeviceRegistry {
  public:
	static DeviceRegistry& Instance() {
		static DeviceRegistry instance;
		return instance;
	}

	void Register(std::string name, DeviceFactory factory) {
		_factories[std::move(name)] = std::move(factory);
	}

	const DeviceFactory* Find(const std::string& name) const {
		auto it = _factories.find(name);
		return it == _factories.end() ? nullptr : &it->second;
	}

	std::vector<std::string> Names() const {
		std::vector<std::string> names;
		for (auto& [name, factory] : _factories)
			names.push_back(name);
		return names;
	}

  private:
	std::map<std::string, DeviceFactory> _factories;
};

// ---- Value parsing/formatting shared by `drive`/`clear`/`expect`/`dump` ----

// Either an exact Sim::LogicState (single-pin state name) or a plain integer
// (bus value, or 0/1 shorthand for a pin). See DeviceHarness.md for the
// grammar's value syntax.
struct ParsedValue {
	bool IsState;
	Sim::LogicState State;
	uint64_t Number;
};

inline ParsedValue ParseValueToken(const std::string& token) {
	if (token == "H") return {true, Sim::LogicState::H, 0};
	if (token == "L") return {true, Sim::LogicState::L, 0};
	if (token == "Hw") return {true, Sim::LogicState::Hw, 0};
	if (token == "Lw") return {true, Sim::LogicState::Lw, 0};
	if (token == "F") return {true, Sim::LogicState::F, 0};

	try {
		return {false, Sim::LogicState::F, static_cast<uint64_t>(std::stoull(token, nullptr, 0))};
	} catch (const std::exception&) {
		throw HarnessError("invalid value '" + token + "' (expected H/L/Hw/Lw/F or a number)");
	}
}

inline void DriveValue(TestPoint& point, const ParsedValue& value) {
	if (std::holds_alternative<Sim::Pin>(point)) {
		Sim::Pin& pin = std::get<Sim::Pin>(point);
		pin.Write(value.IsState ? value.State : Sim::LogicState::ActiveHigh(value.Number != 0));
		return;
	}
	Sim::Bus& bus = std::get<Sim::Bus>(point);
	if (!value.IsState) {
		bus.Write(value.Number);
		return;
	}
	// A state name against a bus means "every bit" -- e.g. `= F` releases a
	// whole data bus at once, as an alternative to a numeric bit pattern.
	for (int i = 0; i < bus.Count(); ++i)
		bus[i].Write(value.State);
}

inline void ClearValue(TestPoint& point) {
	if (std::holds_alternative<Sim::Pin>(point)) std::get<Sim::Pin>(point).Write(Sim::LogicState::F);
	else std::get<Sim::Bus>(point).Clear();
}

inline bool CompareValue(const TestPoint& point, const ParsedValue& expected) {
	if (std::holds_alternative<Sim::Pin>(point)) {
		const Sim::LogicState actual = std::get<Sim::Pin>(point).Read();
		return expected.IsState ? actual == expected.State : actual.IsHigh() == (expected.Number != 0);
	}
	const Sim::Bus& bus = std::get<Sim::Bus>(point);
	if (!expected.IsState) return bus.Read<uint64_t>() == expected.Number;
	for (int i = 0; i < bus.Count(); ++i)
		if (bus[i].Read() != expected.State) return false;
	return true;
}

// True if `point`'s net currently has two conflicting strong drivers (e.g.
// a script `drive` left in place while the device itself also drives the
// same pin) -- Sim::LogicState::E, which nothing in the engine itself
// surfaces at runtime (see AGENTS.md's "6-state logic": "Nothing checks for
// E at runtime"). Checked per-bit for a bus via Bus::operator[], not via
// Bus::Read<uint64_t>(), which collapses a contended bit to 0 and would
// hide exactly the condition this is meant to catch.
inline bool HasContention(const TestPoint& point) {
	if (std::holds_alternative<Sim::Pin>(point)) return std::get<Sim::Pin>(point).Read().IsError();
	const Sim::Bus& bus = std::get<Sim::Bus>(point);
	for (int i = 0; i < bus.Count(); ++i)
		if (bus[i].Read().IsError()) return true;
	return false;
}

// Which bit indices of a bus are currently contended, for a specific
// diagnostic message. Empty when `HasContention` on the same bus is false.
inline std::vector<int> ContentionBits(const Sim::Bus& bus) {
	std::vector<int> bits;
	for (int i = 0; i < bus.Count(); ++i)
		if (bus[i].Read().IsError()) bits.push_back(i);
	return bits;
}

inline std::string FormatHex(uint64_t value) {
	char buf[2 + 16 + 1];
	std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(value));
	return buf;
}

inline std::string FormatPoint(const TestPoint& point) {
	if (std::holds_alternative<Sim::Pin>(point)) return std::get<Sim::Pin>(point).Read().ToString();
	return FormatHex(std::get<Sim::Bus>(point).Read<uint64_t>());
}

// ---- Register/memory value parsing, shared by DeviceHarness's `dumpregs`/
// `peek`/`poke`/`expect reg`/`expect mem` verbs and MachineHarnessScript's
// `<point>.<register>`/`<point>[<addr>]` syntax. Plain numbers only -- unlike
// ParseValueToken above, there's no H/L/Hw/Lw/F vocabulary here: a register
// or memory byte isn't a net, just a value.
inline uint64_t ParseNumericToken(const std::string& token) {
	try {
		return std::stoull(token, nullptr, 0); // base 0: autodetects 0x/0 prefixes
	} catch (const std::exception&) {
		throw HarnessError("invalid numeric value '" + token + "'");
	}
}

// Scans a fresh DumpRegisters() call every time rather than caching it --
// a register's value can change between two lookups, and the vector it
// returns is only ever a temporary.
inline std::optional<uint64_t> FindRegister(Sim::IRegisterInspectable& registers, const std::string& name) {
	for (const auto& [n, v] : registers.DumpRegisters())
		if (n == name) return v;
	return std::nullopt;
}

} // namespace Sim::Testing

// Self-registers a device factory under `typeName` before main() runs. Not
// semicolon-terminated at the call site (matches e.g. gtest's TEST(...)):
// it expands to a namespace-scoped type + static instance, not a statement.
#define HARNESS_CONCAT_INNER(a, b) a##b
#define HARNESS_CONCAT(a, b) HARNESS_CONCAT_INNER(a, b)
#define HARNESS_REGISTER_DEVICE(typeName, factory)                                                                     \
	namespace {                                                                                                        \
	struct HARNESS_CONCAT(HarnessRegistrar_, __LINE__) {                                                               \
		HARNESS_CONCAT(HarnessRegistrar_, __LINE__)() {                                                                \
			::Sim::Testing::DeviceRegistry::Instance().Register(typeName, factory);                                    \
		}                                                                                                              \
	};                                                                                                                 \
	static HARNESS_CONCAT(HarnessRegistrar_, __LINE__) HARNESS_CONCAT(harnessRegistrarInstance_, __LINE__);            \
	}
