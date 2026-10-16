#pragma once
#include "HarnessCore.hpp"
#include "ScriptLexer.hpp"
#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Script macros, shared by DeviceHarness and MachineHarness (see
// DeviceHarness.md's "Macros"). A pre-pass over the script text, before either
// parser sees it:
//
//   def R                 # name; body runs to the matching `end`
//     drive CLK = L
//     expect A = $0       # $0, $1, ... = call arguments, counted from 0
//   end
//   R 0x0200 0xa9         # a line whose first word is a macro name is a call
//
// A call is replaced by the body lines with `$N` substituted, so the parsers
// see only ordinary commands. Arguments are whitespace-separated words; the
// argument count must equal the highest `$N` used plus one. `$$` is a literal
// `$`; a `$` not followed by a digit or `$` is left alone. A body may call
// macros defined earlier. Expanded lines report the call's line number.
// -----------------------------------------------------------------------------
namespace Sim::Testing {

struct SourceLine {
	std::string Text;
	int Line;
};

class MacroExpander {
  public:
	std::vector<SourceLine> Run(const std::string& source) {
		std::istringstream stream(source);
		std::string raw;
		int lineNo = 0;
		Macro* open = nullptr;

		while (std::getline(stream, raw)) {
			++lineNo;
			const std::string line = Trim(raw);
			if (line.empty() || line[0] == '#') continue;
			const auto tokens = Tokenize(StripTrailingComment(line));
			if (tokens.empty()) continue;
			const std::string& head = tokens[0];

			if (open) {
				if (head == "end") {
					open = nullptr;
				} else if (head == "def") {
					throw HarnessError(std::format("line {}: 'def' cannot be nested", lineNo));
				} else {
					// `print` keeps its text literally (including any '#').
					const std::string body = head == "print" ? line : StripTrailingComment(line);
					open->Body.push_back(body);
					open->Arity = std::max(open->Arity, ArgumentsNeeded(body));
				}
			} else if (head == "def") {
				open = &Define(tokens, lineNo);
			} else if (head == "end") {
				throw HarnessError(std::format("line {}: 'end' without 'def'", lineNo));
			} else if (_macros.count(head)) {
				Expand(_macros.at(head), {tokens.begin() + 1, tokens.end()}, lineNo, 0);
			} else {
				_out.push_back({line, lineNo});
			}
		}
		if (open) throw HarnessError(std::format("macro '{}' has no 'end'", open->Name));
		return std::move(_out);
	}

  private:
	struct Macro {
		std::string Name;
		std::vector<std::string> Body;
		size_t Arity = 0;
	};

	std::map<std::string, Macro> _macros;
	std::vector<SourceLine> _out;

	Macro& Define(const std::vector<std::string>& tokens, int lineNo) {
		// Every verb either harness understands: a macro may not shadow one.
		static const std::set<std::string> reserved = {"device", "machine", "drive", "clear",	   "step",
													   "sleep",	 "expect",	"dump",	 "dumpregs",   "peek",
													   "poke",	 "click",	"screenshot", "print", "reset",
													   "run",	 "def",		"end"};
		if (tokens.size() != 2) throw HarnessError(std::format("line {}: expected 'def <name>'", lineNo));
		const std::string& name = tokens[1];
		const bool wellFormed = !std::isdigit(static_cast<unsigned char>(name[0])) &&
								std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
		if (!wellFormed) throw HarnessError(std::format("line {}: bad macro name '{}'", lineNo, name));
		if (reserved.count(name)) throw HarnessError(std::format("line {}: '{}' is a built-in command", lineNo, name));
		if (_macros.count(name)) throw HarnessError(std::format("line {}: macro '{}' already defined", lineNo, name));
		Macro& macro = _macros[name];
		macro.Name = name;
		return macro;
	}

	// Calls `visit(index)` for each `$N` in `text`, in order.
	template <typename F> static void ForEachReference(const std::string& text, F&& visit) {
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] != '$' || i + 1 == text.size()) continue;
			if (text[i + 1] == '$') {
				++i;
				continue;
			}
			size_t end = i + 1;
			while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
			if (end > i + 1) visit(i, end, std::stoul(text.substr(i + 1, end - i - 1)));
			i = end - 1;
		}
	}

	static size_t ArgumentsNeeded(const std::string& text) {
		size_t needed = 0;
		ForEachReference(text, [&](size_t, size_t, size_t index) { needed = std::max(needed, index + 1); });
		return needed;
	}

	static std::string Substitute(const std::string& text, const std::vector<std::string>& args) {
		std::string result;
		size_t copied = 0;
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] != '$' || i + 1 == text.size()) continue;
			if (text[i + 1] == '$') {
				result.append(text, copied, i - copied + 1); // keeps one '$'
				copied = i + 2;
				++i;
				continue;
			}
			size_t end = i + 1;
			while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) ++end;
			if (end == i + 1) continue;
			result.append(text, copied, i - copied);
			result += args[std::stoul(text.substr(i + 1, end - i - 1))];
			copied = end;
			i = end - 1;
		}
		result.append(text, copied, std::string::npos);
		return result;
	}

	void Expand(const Macro& macro, const std::vector<std::string>& args, int lineNo, int depth) {
		if (depth > 16) throw HarnessError(std::format("line {}: macro '{}' nests too deeply", lineNo, macro.Name));
		if (args.size() != macro.Arity)
			throw HarnessError(
				std::format("line {}: macro '{}' takes {} argument(s), got {}", lineNo, macro.Name, macro.Arity, args.size())
			);
		for (const std::string& body : macro.Body) {
			const std::string line = Substitute(body, args);
			const auto tokens = Tokenize(StripTrailingComment(line));
			if (!tokens.empty() && _macros.count(tokens[0]))
				Expand(_macros.at(tokens[0]), {tokens.begin() + 1, tokens.end()}, lineNo, depth + 1);
			else _out.push_back({line, lineNo});
		}
	}
};

inline std::vector<SourceLine> ExpandMacros(const std::string& source) {
	return MacroExpander().Run(source);
}

} // namespace Sim::Testing
