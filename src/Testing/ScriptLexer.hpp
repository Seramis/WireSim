#pragma once
#include <sstream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Shared lexing helpers for both DeviceHarness's and MachineHarness's script
// parsers (HarnessScript.cpp / MachineHarnessScript.cpp). Comment-stripping
// and whitespace tokenizing have nothing script-specific about them, so both
// parsers include this instead of duplicating it.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

inline std::string Trim(const std::string& s) {
	const auto first = s.find_first_not_of(" \t\r");
	if (first == std::string::npos) return "";
	const auto last = s.find_last_not_of(" \t\r");
	return s.substr(first, last - first + 1);
}

inline std::vector<std::string> Tokenize(const std::string& line) {
	std::vector<std::string> tokens;
	std::istringstream iss(line);
	std::string tok;
	while (iss >> tok)
		tokens.push_back(tok);
	return tokens;
}

// Strips a trailing `# ...` comment. Callers special-case `print` before
// ever reaching this -- that verb takes the rest of its line literally, so
// it must never have a comment stripped from it.
inline std::string StripTrailingComment(const std::string& line) {
	std::string result = line;
	const auto hashPos = result.find('#');
	if (hashPos != std::string::npos) result = Trim(result.substr(0, hashPos));
	return result;
}

} // namespace Sim::Testing
