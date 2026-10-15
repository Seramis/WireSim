#include "HarnessScript.hpp"
#include "ScriptLexer.hpp"
#include <chrono>
#include <format>
#include <ostream>
#include <sstream>
#include <thread>

namespace Sim::Testing {

namespace {

enum class Verb {
	Device,
	Drive,
	Clear,
	Step,
	Expect,
	ExpectReg,
	ExpectMem,
	Dump,
	DumpRegs,
	Peek,
	Poke,
	Print,
	Click,
	Screenshot,
	Sleep
};

struct Command {
	Verb verb;
	int line = 0;
	std::string typeName, pin, valueToken, message, text;
	ArgsMap args;
	int stepCount = 1;
	int x = 0, y = 0;
	std::string button = "left"; // Click only
	std::string path;			 // Screenshot only
	int sleepMs = 0;			 // Sleep only
	std::string regName;		 // ExpectReg only
	uint64_t address = 0;		 // Peek/Poke/ExpectMem only
};

ArgsMap ParseArgs(const std::vector<std::string>& tokens, size_t from, int lineNo) {
	ArgsMap args;
	for (size_t i = from; i < tokens.size(); ++i) {
		const auto eq = tokens[i].find('=');
		if (eq == std::string::npos)
			throw HarnessError(std::format("line {}: expected key=value, got '{}'", lineNo, tokens[i]));
		args[tokens[i].substr(0, eq)] = ArgValue{tokens[i].substr(eq + 1)};
	}
	return args;
}

// Parses the whole script up front, so a syntax error anywhere is reported
// before any device is created or any cycle is run.
std::vector<Command> ParseScript(const std::string& source) {
	std::vector<Command> commands;
	std::istringstream stream(source);
	std::string rawLine;
	int lineNo = 0;
	bool sawDevice = false;

	while (std::getline(stream, rawLine)) {
		++lineNo;
		const std::string line = Trim(rawLine);
		if (line.empty() || line[0] == '#') continue;

		std::istringstream verbStream(line);
		std::string verbTok;
		verbStream >> verbTok;

		Command cmd;
		cmd.line = lineNo;

		if (verbTok == "print") {
			cmd.verb = Verb::Print;
			cmd.text = Trim(line.substr(verbTok.size()));
			commands.push_back(std::move(cmd));
			continue;
		}

		const std::string codePart = StripTrailingComment(line);

		const auto tokens = Tokenize(codePart);
		if (tokens.empty()) continue;
		const std::string& verb = tokens[0];

		if (verb == "device") {
			if (tokens.size() < 2)
				throw HarnessError(std::format("line {}: expected 'device <Type> [key=value...]'", lineNo));
			if (sawDevice)
				throw HarnessError(
					std::format(
						"line {}: a script may only declare one 'device' -- the harness is single-device only", lineNo
					)
				);
			sawDevice = true;
			cmd.verb = Verb::Device;
			cmd.typeName = tokens[1];
			cmd.args = ParseArgs(tokens, 2, lineNo);
		} else if (verb == "drive") {
			if (tokens.size() < 4 || tokens[2] != "=")
				throw HarnessError(std::format("line {}: expected 'drive <pin> = <value>'", lineNo));
			cmd.verb = Verb::Drive;
			cmd.pin = tokens[1];
			cmd.valueToken = tokens[3];
		} else if (verb == "clear") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'clear <pin>'", lineNo));
			cmd.verb = Verb::Clear;
			cmd.pin = tokens[1];
		} else if (verb == "step") {
			if (tokens.size() > 2) throw HarnessError(std::format("line {}: expected 'step [count]'", lineNo));
			cmd.verb = Verb::Step;
			cmd.stepCount = tokens.size() > 1 ? std::stoi(tokens[1]) : 1;
			if (cmd.stepCount < 1) throw HarnessError(std::format("line {}: step count must be >= 1", lineNo));
		} else if (verb == "sleep") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'sleep <milliseconds>'", lineNo));
			cmd.verb = Verb::Sleep;
			try {
				cmd.sleepMs = std::stoi(tokens[1]);
			} catch (const std::exception&) {
				throw HarnessError(std::format("line {}: sleep milliseconds must be an integer", lineNo));
			}
			if (cmd.sleepMs < 0) throw HarnessError(std::format("line {}: sleep milliseconds must be >= 0", lineNo));
		} else if (verb == "expect") {
			// Three subforms share one verb: a bare pin (unchanged, keeps
			// an optional trailing message), `reg.<name>` (via
			// IRegisterInspectable), and `mem[<addr>]` (via
			// IMemoryInspectable) -- see HarnessCore.hpp's Registers/Memory
			// capability pointers. The reg/mem forms take no message: they're
			// a fixed-shape value assertion, not a free-form check.
			if (tokens.size() < 2)
				throw HarnessError(std::format("line {}: expected 'expect <pin> = <value> [message...]'", lineNo));
			if (tokens[1].starts_with("reg.")) {
				if (tokens.size() != 4 || tokens[2] != "=")
					throw HarnessError(std::format("line {}: expected 'expect reg.<name> = <value>'", lineNo));
				cmd.verb = Verb::ExpectReg;
				cmd.regName = tokens[1].substr(4);
				cmd.valueToken = tokens[3];
			} else if (tokens[1].starts_with("mem[") && tokens[1].ends_with("]")) {
				if (tokens.size() != 4 || tokens[2] != "=")
					throw HarnessError(std::format("line {}: expected 'expect mem[<addr>] = <value>'", lineNo));
				cmd.address = ParseNumericToken(tokens[1].substr(4, tokens[1].size() - 5));
				cmd.verb = Verb::ExpectMem;
				cmd.valueToken = tokens[3];
			} else {
				if (tokens.size() < 4 || tokens[2] != "=")
					throw HarnessError(std::format("line {}: expected 'expect <pin> = <value> [message...]'", lineNo));
				cmd.verb = Verb::Expect;
				cmd.pin = tokens[1];
				cmd.valueToken = tokens[3];
				for (size_t i = 4; i < tokens.size(); ++i) {
					if (!cmd.message.empty()) cmd.message += ' ';
					cmd.message += tokens[i];
				}
			}
		} else if (verb == "dump") {
			if (tokens.size() != 1)
				throw HarnessError(
					std::format("line {}: expected 'dump' (no arguments -- the harness is single-device only)", lineNo)
				);
			cmd.verb = Verb::Dump;
		} else if (verb == "dumpregs") {
			if (tokens.size() != 1)
				throw HarnessError(std::format("line {}: expected 'dumpregs' (no arguments)", lineNo));
			cmd.verb = Verb::DumpRegs;
		} else if (verb == "peek") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'peek <addr>'", lineNo));
			cmd.verb = Verb::Peek;
			cmd.address = ParseNumericToken(tokens[1]);
		} else if (verb == "poke") {
			if (tokens.size() != 3) throw HarnessError(std::format("line {}: expected 'poke <addr> <value>'", lineNo));
			cmd.verb = Verb::Poke;
			cmd.address = ParseNumericToken(tokens[1]);
			cmd.valueToken = tokens[2];
		} else if (verb == "click") {
			if (tokens.size() < 3 || tokens.size() > 4)
				throw HarnessError(std::format("line {}: expected 'click <x> <y> [left|right|middle]'", lineNo));
			cmd.verb = Verb::Click;
			try {
				cmd.x = std::stoi(tokens[1]);
				cmd.y = std::stoi(tokens[2]);
			} catch (const std::exception&) {
				throw HarnessError(std::format("line {}: click x/y must be integers", lineNo));
			}
			cmd.button = tokens.size() > 3 ? tokens[3] : "left";
			if (cmd.button != "left" && cmd.button != "right" && cmd.button != "middle")
				throw HarnessError(
					std::format("line {}: click button must be left/right/middle, got '{}'", lineNo, cmd.button)
				);
		} else if (verb == "screenshot") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'screenshot <path>'", lineNo));
			cmd.verb = Verb::Screenshot;
			cmd.path = tokens[1];
		} else {
			throw HarnessError(std::format("line {}: unknown command '{}'", lineNo, verb));
		}

		commands.push_back(std::move(cmd));
	}
	return commands;
}

bool IsSetupVerb(Verb v) {
	return v == Verb::Device;
}
bool IsRunOnlyVerb(Verb v) {
	return !IsSetupVerb(v) && v != Verb::Print;
}

// A command's own report line starts with this: the step counter (as
// before) plus milliseconds of real wall-clock time elapsed since the
// script's run phase began. The two diverge whenever `sleep` or a device's
// own background thread makes real time pass without a `step` -- that gap
// is the whole reason this exists; see DeviceHarness.md's "sleep" note.
std::string Prefix(int stepCounter, long long elapsedMs) {
	return std::format("[s={} t={}]", stepCounter, elapsedMs);
}

void DumpDevice(std::ostream& out, const std::string& prefix, const DeviceHandle& handle) {
	for (auto& [name, point] : handle.Points)
		out << std::format("{} dump {} = {}\n", prefix, name, FormatPoint(point));
}

// Flags any pin/bus-bit currently driven to Sim::LogicState::E -- two
// conflicting strong drivers on the same net, almost always a script that
// forgot to `clear` a pin before expecting the device to drive it back (see
// DeviceHarness.md's "Contention detection"). Purely advisory: it doesn't
// affect pass/fail counts, since a script could in principle be deliberately
// exercising bus-fight behavior.
void ReportContention(std::ostream& out, const std::string& prefix, const DeviceHandle& handle) {
	for (auto& [name, point] : handle.Points) {
		if (std::holds_alternative<Sim::Pin>(point)) {
			if (HasContention(point))
				out << std::format(
					"{} CONTENTION {} (driven H and L at once -- check for a missing `clear`)\n", prefix, name
				);
			continue;
		}
		const auto bits = ContentionBits(std::get<Sim::Bus>(point));
		if (bits.empty()) continue;
		std::string list;
		for (size_t i = 0; i < bits.size(); ++i) {
			if (i) list += ",";
			list += std::to_string(bits[i]);
		}
		out << std::format(
			"{} CONTENTION {} bit(s) {} (driven H and L at once -- check for a missing `clear`)\n", prefix, name, list
		);
	}
}

} // namespace

RunResult RunScript(Harness& harness, const std::string& source, std::ostream& out) {
	const auto commands = ParseScript(source);

	// Setup (device) must fully precede execution (drive/clear/step/expect/
	// dump) -- the same bake-then-run contract Platform itself enforces (see
	// overview.md). `print` is exempt either way: it has no topology/
	// execution effect, so it can't violate this ordering.
	std::vector<Command> setupCommands, runCommands;
	bool seenRunOnly = false;
	for (const auto& cmd : commands) {
		if (IsSetupVerb(cmd.verb)) {
			if (seenRunOnly)
				throw HarnessError(
					std::format("line {}: 'device' must appear before step/drive/expect/dump", cmd.line)
				);
			setupCommands.push_back(cmd);
		} else {
			if (IsRunOnlyVerb(cmd.verb)) seenRunOnly = true;
			runCommands.push_back(cmd);
		}
	}

	for (const auto& cmd : setupCommands) {
		harness.CreateDevice(cmd.typeName, cmd.args);
		out << std::format("device {}\n", cmd.typeName);
	}

	// Pre-flight: resolve every run-phase pin reference now, so an unknown
	// pin (or a missing 'device' line) is reported before anything is
	// stepped, not mid-run. click/screenshot get the same treatment against
	// Ui instead of a pin name.
	for (const auto& cmd : runCommands) {
		if (cmd.verb == Verb::Drive || cmd.verb == Verb::Clear || cmd.verb == Verb::Expect) harness.Resolve(cmd.pin);
		if (cmd.verb == Verb::Click || cmd.verb == Verb::Screenshot) {
			DeviceHandle* device = harness.Device();
			if (!device || !device->Ui)
				throw HarnessError(
					std::format(
						"line {}: device has no UI (click/screenshot need a device inheriting SdlUiTestHooks)", cmd.line
					)
				);
		}
		if (cmd.verb == Verb::DumpRegs || cmd.verb == Verb::ExpectReg) {
			DeviceHandle* device = harness.Device();
			if (!device || !device->Registers)
				throw HarnessError(
					std::format(
						"line {}: device has no registers (dumpregs/expect reg need a device implementing "
						"IRegisterInspectable)",
						cmd.line
					)
				);
			if (cmd.verb == Verb::ExpectReg && !FindRegister(*device->Registers, cmd.regName))
				throw HarnessError(std::format("line {}: no such register '{}'", cmd.line, cmd.regName));
		}
		if (cmd.verb == Verb::Peek || cmd.verb == Verb::Poke || cmd.verb == Verb::ExpectMem) {
			DeviceHandle* device = harness.Device();
			if (!device || !device->Memory)
				throw HarnessError(
					std::format(
						"line {}: device has no memory (peek/poke/expect mem need a device implementing "
						"IMemoryInspectable)",
						cmd.line
					)
				);
			if (cmd.address >= device->Memory->InspectableSize())
				throw HarnessError(
					std::format(
						"line {}: address {} out of range (size {})",
						cmd.line,
						cmd.address,
						device->Memory->InspectableSize()
					)
				);
		}
	}

	RunResult result;
	size_t idx = 0;
	int pendingSteps = 0;
	int stepCounter = 0;

	// Epoch for every report line's `t=` field (see Prefix above). Captured
	// here, not earlier: setup (device construction, preflight) isn't part
	// of the run phase `t=` is meant to measure, and a device's own
	// background thread -- the reason this exists -- doesn't start running
	// against the script's timeline until this point either.
	const auto scriptClockStart = std::chrono::steady_clock::now();
	auto elapsedMs = [&] {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - scriptClockStart
		)
			.count();
	};

	harness.PlatformRef().Start([&] {
		// Reflects the just-completed cycle's resolved net values (Platform
		// calls onBeforeCycle, *then* Cycle() -- see Platform::Start), so
		// this sees the result of whichever `step` most recently ran, before
		// this invocation does anything else. Harmless/no-op on the very
		// first call, before any device exists or any cycle has run.
		if (DeviceHandle* device = harness.Device()) ReportContention(out, Prefix(stepCounter, elapsedMs()), *device);

		while (true) {
			if (pendingSteps > 0) {
				--pendingSteps;
				++stepCounter;
				out << std::format("{} step\n", Prefix(stepCounter, elapsedMs()));
				return; // let this cycle actually run
			}
			if (idx >= runCommands.size()) {
				harness.PlatformRef().Stop();
				return;
			}

			const Command& cmd = runCommands[idx++];
			switch (cmd.verb) {
				case Verb::Step: pendingSteps = cmd.stepCount; continue;
				case Verb::Print: out << cmd.text << "\n"; continue;
				case Verb::Sleep: {
					// Blocks this thread for real wall-clock time -- no Cycle()
					// runs while asleep. A background thread spawned by the
					// device under test (e.g. a construction-time real-time
					// timer) keeps running and keeps calling Platform::Enqueue()
					// on its own schedule regardless; the next step/drive/expect
					// after this sleep sees whatever it queued, drained on the
					// following Cycle()'s external-command phase.
					std::this_thread::sleep_for(std::chrono::milliseconds(cmd.sleepMs));
					out << std::format("{} sleep {}ms\n", Prefix(stepCounter, elapsedMs()), cmd.sleepMs);
					continue;
				}
				case Verb::Drive: {
					TestPoint& point = harness.Resolve(cmd.pin);
					DriveValue(point, ParseValueToken(cmd.valueToken));
					out << std::format("{} drive {} = {}\n", Prefix(stepCounter, elapsedMs()), cmd.pin, cmd.valueToken);
					continue;
				}
				case Verb::Clear: {
					ClearValue(harness.Resolve(cmd.pin));
					out << std::format("{} clear {}\n", Prefix(stepCounter, elapsedMs()), cmd.pin);
					continue;
				}
				case Verb::Expect: {
					TestPoint& point = harness.Resolve(cmd.pin);
					const ParsedValue expected = ParseValueToken(cmd.valueToken);
					const bool ok = CompareValue(point, expected);
					const std::string actual = FormatPoint(point);
					if (ok) {
						++result.Passed;
						out << std::format(
							"{} PASS {} == {} {}\n",
							Prefix(stepCounter, elapsedMs()),
							cmd.pin,
							cmd.valueToken,
							cmd.message
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL {} expected={} actual={} {}\n",
							Prefix(stepCounter, elapsedMs()),
							cmd.pin,
							cmd.valueToken,
							actual,
							cmd.message
						);
					}
					continue;
				}
				case Verb::Dump: {
					if (DeviceHandle* device = harness.Device())
						DumpDevice(out, Prefix(stepCounter, elapsedMs()), *device);
					continue;
				}
				case Verb::DumpRegs: {
					DeviceHandle* device = harness.Device(); // preflight already guaranteed Registers is non-null
					for (const auto& [name, value] : device->Registers->DumpRegisters())
						out << std::format(
							"{} dumpregs {} = {}\n", Prefix(stepCounter, elapsedMs()), name, FormatHex(value)
						);
					continue;
				}
				case Verb::Peek: {
					DeviceHandle* device = harness.Device(); // preflight already guaranteed Memory is non-null
					const uint8_t value = device->Memory->Peek(static_cast<uint32_t>(cmd.address));
					out << std::format(
						"{} peek [{}] = {}\n",
						Prefix(stepCounter, elapsedMs()),
						FormatHex(cmd.address),
						FormatHex(value)
					);
					continue;
				}
				case Verb::Poke: {
					DeviceHandle* device = harness.Device(); // preflight already guaranteed Memory is non-null
					const uint64_t value = ParseNumericToken(cmd.valueToken);
					device->Memory->Poke(static_cast<uint32_t>(cmd.address), static_cast<uint8_t>(value));
					out << std::format(
						"{} poke [{}] = {}\n", Prefix(stepCounter, elapsedMs()), FormatHex(cmd.address), cmd.valueToken
					);
					continue;
				}
				case Verb::ExpectReg: {
					DeviceHandle* device = harness.Device(); // preflight already guaranteed Registers/name exist
					const uint64_t expected = ParseNumericToken(cmd.valueToken);
					const uint64_t actual = *FindRegister(*device->Registers, cmd.regName);
					if (actual == expected) {
						++result.Passed;
						out << std::format(
							"{} PASS reg.{} == {}\n", Prefix(stepCounter, elapsedMs()), cmd.regName, cmd.valueToken
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL reg.{} expected={} actual={}\n",
							Prefix(stepCounter, elapsedMs()),
							cmd.regName,
							cmd.valueToken,
							FormatHex(actual)
						);
					}
					continue;
				}
				case Verb::ExpectMem: {
					DeviceHandle* device = harness.Device(); // preflight already guaranteed Memory is non-null
					const uint64_t expected = ParseNumericToken(cmd.valueToken);
					const uint8_t actual = device->Memory->Peek(static_cast<uint32_t>(cmd.address));
					if (actual == expected) {
						++result.Passed;
						out << std::format(
							"{} PASS mem[{}] == {}\n",
							Prefix(stepCounter, elapsedMs()),
							FormatHex(cmd.address),
							cmd.valueToken
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL mem[{}] expected={} actual={}\n",
							Prefix(stepCounter, elapsedMs()),
							FormatHex(cmd.address),
							cmd.valueToken,
							FormatHex(actual)
						);
					}
					continue;
				}
				case Verb::Click: {
					SdlUiTestHooks* ui = harness.Device()->Ui; // preflight already guaranteed non-null
					const UiMouseButton button = cmd.button == "right"	  ? UiMouseButton::Right
												 : cmd.button == "middle" ? UiMouseButton::Middle
																		  : UiMouseButton::Left;
					const bool ok = ui->Click(cmd.x, cmd.y, button);
					if (ok) {
						++result.Passed;
						out << std::format(
							"{} PASS click {} {} {}\n", Prefix(stepCounter, elapsedMs()), cmd.x, cmd.y, cmd.button
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL click {} {} {} (no window ever opened)\n",
							Prefix(stepCounter, elapsedMs()),
							cmd.x,
							cmd.y,
							cmd.button
						);
					}
					continue;
				}
				case Verb::Screenshot: {
					SdlUiTestHooks* ui = harness.Device()->Ui; // preflight already guaranteed non-null
					const bool ok = ui->CaptureScreenshot(cmd.path);
					if (ok) {
						++result.Passed;
						out << std::format("{} PASS screenshot {}\n", Prefix(stepCounter, elapsedMs()), cmd.path);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL screenshot {} (timeout or save failed)\n",
							Prefix(stepCounter, elapsedMs()),
							cmd.path
						);
					}
					continue;
				}
				default: continue; // Device already ran in the setup phase above
			}
		}
	});

	out << std::format("{}/{} checks passed\n", result.Passed, result.Total());
	return result;
}

} // namespace Sim::Testing
