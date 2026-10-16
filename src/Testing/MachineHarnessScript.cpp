#include "MachineHarnessScript.hpp"
#include "ScriptLexer.hpp"
#include "ScriptMacros.hpp"
#include <chrono>
#include <format>
#include <ostream>
#include <sstream>

namespace Sim::Testing {

namespace {

enum class Verb {
	Machine,
	Reset,
	Run,
	Poke,
	ExpectReg,
	ExpectMem,
	Dump,
	Print
};

struct Command {
	Verb verb;
	int line = 0;
	std::string typeName;
	ArgsMap args;
	int halfCycles = 10;	// Reset only -- default mirrors My6502's _resetCycles
	uint64_t cpuCycles = 0; // Run only
	std::string point, regName, valueToken, message, text;
	uint64_t address = 0;
};

// Splits "point.register" into (point, register). Returns false if `ref`
// has no '.'.
bool SplitRegisterRef(const std::string& ref, std::string& point, std::string& regName) {
	const auto dot = ref.find('.');
	if (dot == std::string::npos) return false;
	point = ref.substr(0, dot);
	regName = ref.substr(dot + 1);
	return true;
}

// Splits "point[addr]" into (point, addr). Returns false if `ref` isn't
// shaped that way.
bool SplitMemoryRef(const std::string& ref, std::string& point, uint64_t& address) {
	const auto bracket = ref.find('[');
	if (bracket == std::string::npos || !ref.ends_with("]")) return false;
	point = ref.substr(0, bracket);
	address = ParseNumericToken(ref.substr(bracket + 1, ref.size() - bracket - 2));
	return true;
}

std::vector<Command> ParseScript(const std::string& sourceText) {
	std::vector<Command> commands;
	bool sawMachine = false;

	for (const SourceLine& source : ExpandMacros(sourceText)) {
		const int lineNo = source.Line;
		const std::string line = Trim(source.Text);
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

		auto buildMessage = [&](size_t from) {
			std::string message;
			for (size_t i = from; i < tokens.size(); ++i) {
				if (!message.empty()) message += ' ';
				message += tokens[i];
			}
			return message;
		};

		if (verb == "machine") {
			if (tokens.size() < 2)
				throw HarnessError(std::format("line {}: expected 'machine <Type> rom=<DIR> [key=value...]'", lineNo));
			if (sawMachine)
				throw HarnessError(
					std::format(
						"line {}: a script may only declare one 'machine' -- the harness is single-machine only", lineNo
					)
				);
			sawMachine = true;
			cmd.verb = Verb::Machine;
			cmd.typeName = tokens[1];
			for (size_t i = 2; i < tokens.size(); ++i) {
				const auto eq = tokens[i].find('=');
				if (eq == std::string::npos)
					throw HarnessError(std::format("line {}: expected key=value, got '{}'", lineNo, tokens[i]));
				cmd.args[tokens[i].substr(0, eq)] = ArgValue{tokens[i].substr(eq + 1)};
			}
			if (!cmd.args.count("rom"))
				throw HarnessError(std::format("line {}: 'machine' requires rom=<DIR>", lineNo));
		} else if (verb == "reset") {
			if (tokens.size() > 2) throw HarnessError(std::format("line {}: expected 'reset [halfcycles]'", lineNo));
			cmd.verb = Verb::Reset;
			if (tokens.size() > 1) {
				try {
					cmd.halfCycles = std::stoi(tokens[1]);
				} catch (const std::exception&) {
					throw HarnessError(std::format("line {}: reset halfcycles must be an integer", lineNo));
				}
			}
			if (cmd.halfCycles < 1) throw HarnessError(std::format("line {}: reset halfcycles must be >= 1", lineNo));
		} else if (verb == "run") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'run <cpuCycles>'", lineNo));
			cmd.verb = Verb::Run;
			cmd.cpuCycles = ParseNumericToken(tokens[1]);
			if (cmd.cpuCycles < 1) throw HarnessError(std::format("line {}: run cpuCycles must be >= 1", lineNo));
		} else if (verb == "poke") {
			if (tokens.size() < 4 || tokens[2] != "=")
				throw HarnessError(std::format("line {}: expected 'poke <point>[<addr>] = <value>'", lineNo));
			if (!SplitMemoryRef(tokens[1], cmd.point, cmd.address))
				throw HarnessError(std::format("line {}: expected 'poke <point>[<addr>] = <value>'", lineNo));
			cmd.verb = Verb::Poke;
			cmd.valueToken = tokens[3];
		} else if (verb == "expect") {
			if (tokens.size() < 4 || tokens[2] != "=")
				throw HarnessError(
					std::format(
						"line {}: expected 'expect <point>.<register> = <value>' or 'expect <point>[<addr>] = <value>'",
						lineNo
					)
				);
			if (SplitRegisterRef(tokens[1], cmd.point, cmd.regName)) {
				cmd.verb = Verb::ExpectReg;
			} else if (SplitMemoryRef(tokens[1], cmd.point, cmd.address)) {
				cmd.verb = Verb::ExpectMem;
			} else {
				throw HarnessError(
					std::format(
						"line {}: expected 'expect <point>.<register> = <value>' or 'expect <point>[<addr>] = <value>'",
						lineNo
					)
				);
			}
			cmd.valueToken = tokens[3];
			cmd.message = buildMessage(4);
		} else if (verb == "dump") {
			if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'dump <point>'", lineNo));
			cmd.verb = Verb::Dump;
			cmd.point = tokens[1];
		} else {
			throw HarnessError(std::format("line {}: unknown command '{}'", lineNo, verb));
		}

		commands.push_back(std::move(cmd));
	}
	return commands;
}

bool IsSetupVerb(Verb v) {
	return v == Verb::Machine;
}
bool IsRunOnlyVerb(Verb v) {
	return !IsSetupVerb(v) && v != Verb::Print;
}

std::string Prefix(uint64_t cpuCycleCounter, long long elapsedMs) {
	return std::format("[c={} t={}]", cpuCycleCounter, elapsedMs);
}

} // namespace

MachineRunResult RunMachineScript(MachineHarness& harness, const std::string& source, std::ostream& out) {
	const auto commands = ParseScript(source);

	// Setup (machine) must fully precede execution -- same bake-then-run
	// discipline as DeviceHarness's own RunScript (HarnessScript.cpp).
	std::vector<Command> setupCommands, runCommands;
	bool seenRunOnly = false;
	for (const auto& cmd : commands) {
		if (IsSetupVerb(cmd.verb)) {
			if (seenRunOnly)
				throw HarnessError(
					std::format("line {}: 'machine' must appear before reset/run/expect/dump", cmd.line)
				);
			setupCommands.push_back(cmd);
		} else {
			if (IsRunOnlyVerb(cmd.verb)) seenRunOnly = true;
			runCommands.push_back(cmd);
		}
	}
	if (setupCommands.empty()) throw HarnessError("script needs a 'machine <Type> rom=<DIR>' line");

	harness.CreateMachine(setupCommands[0].typeName, setupCommands[0].args);
	out << std::format("machine {}\n", setupCommands[0].typeName);

	// Boot (load the already-built ROM directory) once, before Start() --
	// direct writes into a memory device's storage, not netlist wiring, so
	// this is safe pre-bake, same as My6502::LoadRom running before
	// My6502::Start launches the sim thread.
	const std::string romDir = setupCommands[0].args.at("rom").AsString();
	harness.Machine()->Boot(romDir);
	out << std::format("boot {}\n", romDir);

	// Pre-flight: resolve every run-phase point reference now, so an
	// unknown point/register/out-of-range address is reported before
	// anything runs, not mid-run.
	for (const auto& cmd : runCommands) {
		if (cmd.verb == Verb::ExpectReg || cmd.verb == Verb::Dump) {
			InspectionPoint& point = harness.Resolve(cmd.point);
			if (cmd.verb == Verb::ExpectReg) {
				if (!point.Registers)
					throw HarnessError(
						std::format(
							"line {}: point '{}' has no registers (not IRegisterInspectable)", cmd.line, cmd.point
						)
					);
				if (!FindRegister(*point.Registers, cmd.regName))
					throw HarnessError(std::format("line {}: no such register '{}'", cmd.line, cmd.regName));
			}
		}
		if (cmd.verb == Verb::ExpectMem || cmd.verb == Verb::Poke) {
			InspectionPoint& point = harness.Resolve(cmd.point);
			if (!point.Memory)
				throw HarnessError(
					std::format("line {}: point '{}' has no memory (not IMemoryInspectable)", cmd.line, cmd.point)
				);
			if (cmd.address >= point.Memory->InspectableSize())
				throw HarnessError(
					std::format(
						"line {}: address {} out of range (size {})",
						cmd.line,
						cmd.address,
						point.Memory->InspectableSize()
					)
				);
		}
	}

	MachineRunResult result;
	size_t idx = 0;
	uint64_t pendingCpuCycles = 0;
	uint64_t cpuCycleCounter = 0;

	const auto scriptClockStart = std::chrono::steady_clock::now();
	auto elapsedMs = [&] {
		return std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::steady_clock::now() - scriptClockStart
		)
			.count();
	};

	MachineHandle* machine = harness.Machine();
	machine->PlatformOwner->Start([&] {
		while (true) {
			if (pendingCpuCycles > 0) {
				const bool boundary = machine->TickHalfCycle();
				if (boundary) {
					--pendingCpuCycles;
					++cpuCycleCounter;
				}
				return; // let this half-cycle's Cycle() actually run
			}
			if (idx >= runCommands.size()) {
				machine->PlatformOwner->Stop();
				return;
			}

			const Command& cmd = runCommands[idx++];
			switch (cmd.verb) {
				case Verb::Reset: {
					machine->HoldReset(cmd.halfCycles);
					out << std::format("{} reset {}\n", Prefix(cpuCycleCounter, elapsedMs()), cmd.halfCycles);
					continue;
				}
				case Verb::Run: {
					pendingCpuCycles = cmd.cpuCycles;
					continue;
				}
				case Verb::Print: {
					out << cmd.text << "\n";
					continue;
				}
				case Verb::Poke: {
					InspectionPoint& point = harness.Resolve(cmd.point); // preflight already validated
					const uint64_t value = ParseNumericToken(cmd.valueToken);
					point.Memory->Poke(static_cast<uint32_t>(cmd.address), static_cast<uint8_t>(value));
					out << std::format(
						"{} poke {}[{}] = {}\n",
						Prefix(cpuCycleCounter, elapsedMs()),
						cmd.point,
						FormatHex(cmd.address),
						cmd.valueToken
					);
					continue;
				}
				case Verb::ExpectReg: {
					InspectionPoint& point = harness.Resolve(cmd.point); // preflight already validated
					const uint64_t expected = ParseNumericToken(cmd.valueToken);
					const uint64_t actual = *FindRegister(*point.Registers, cmd.regName);
					if (actual == expected) {
						++result.Passed;
						out << std::format(
							"{} PASS {}.{} == {} {}\n",
							Prefix(cpuCycleCounter, elapsedMs()),
							cmd.point,
							cmd.regName,
							cmd.valueToken,
							cmd.message
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL {}.{} expected={} actual={} {}\n",
							Prefix(cpuCycleCounter, elapsedMs()),
							cmd.point,
							cmd.regName,
							cmd.valueToken,
							FormatHex(actual),
							cmd.message
						);
					}
					continue;
				}
				case Verb::ExpectMem: {
					InspectionPoint& point = harness.Resolve(cmd.point); // preflight already validated
					const uint64_t expected = ParseNumericToken(cmd.valueToken);
					const uint8_t actual = point.Memory->Peek(static_cast<uint32_t>(cmd.address));
					if (actual == expected) {
						++result.Passed;
						out << std::format(
							"{} PASS {}[{}] == {} {}\n",
							Prefix(cpuCycleCounter, elapsedMs()),
							cmd.point,
							FormatHex(cmd.address),
							cmd.valueToken,
							cmd.message
						);
					} else {
						++result.Failed;
						out << std::format(
							"{} FAIL {}[{}] expected={} actual={} {}\n",
							Prefix(cpuCycleCounter, elapsedMs()),
							cmd.point,
							FormatHex(cmd.address),
							cmd.valueToken,
							FormatHex(actual),
							cmd.message
						);
					}
					continue;
				}
				case Verb::Dump: {
					InspectionPoint& point = harness.Resolve(cmd.point); // preflight already validated
					const std::string prefix = Prefix(cpuCycleCounter, elapsedMs());
					if (point.Registers) {
						for (const auto& [name, value] : point.Registers->DumpRegisters())
							out << std::format("{} dump {}.{} = {}\n", prefix, cmd.point, name, FormatHex(value));
					}
					if (point.Memory) {
						// Full contents aren't dumped byte-by-byte -- an
						// arbitrary memory region can be far too large to
						// print usefully; use `expect <point>[<addr>]` for
						// individual addresses instead.
						out << std::format(
							"{} dump {} = <memory, {} bytes>\n", prefix, cmd.point, point.Memory->InspectableSize()
						);
					}
					continue;
				}
				default: continue; // Machine already created in the setup phase above
			}
		}
	});

	out << std::format("{}/{} checks passed\n", result.Passed, result.Total());
	return result;
}

} // namespace Sim::Testing
