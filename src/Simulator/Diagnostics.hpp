#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <format>
#include <iomanip>
#include <print>
#include <sstream>
#include <string>
#include <utility>

namespace Sim {

class Diagnostics {
	using Clock = std::chrono::steady_clock;

	struct Sample {
		Clock::time_point time;
		uint64_t ticks;
	};

	static constexpr uint64_t tickBatchSize = 10000;
	static constexpr auto window = std::chrono::seconds(1);

	std::deque<Sample> _samples;
	uint64_t _ticks = 0;
	uint64_t _pendingTicks = 0;

	void tickWork() {
		_ticks += _pendingTicks;
		_pendingTicks = 0;
		const auto now = Clock::now();
		_samples.push_back({now, _ticks});
		const auto cutoff = now - window;
		while (_samples.size() > 1 && _samples[1].time <= cutoff) {
			_samples.pop_front();
		}
	}

  public:
	bool debugMode = false;
	uint64_t halfCycles = 0;
	uint64_t cpuCycles = 0;

	Diagnostics() {
		_samples.push_back({Clock::now(), 0});
	}

	inline void tick(bool isCpuCycle) {
		++halfCycles;
		if (isCpuCycle) ++cpuCycles;
		if (++_pendingTicks < tickBatchSize) [[likely]]
			return;

		tickWork();
	}

	double GetFrequency() const {
		auto now = Clock::now();
		auto cutoff = now - window;
		const Sample* sample = &_samples.front();
		for (const auto& candidate : _samples) {
			if (candidate.time > cutoff) break;
			sample = &candidate;
		}

		auto elapsed = std::chrono::duration<double>(now - sample->time).count();
		uint64_t measuredTicks = _ticks + _pendingTicks - sample->ticks;
		return elapsed > 0.0 ? measuredTicks / elapsed : 0.0;
	}

	std::string GetFormattedFreq() const {
		double freq = GetFrequency();
		std::stringstream ss;
		ss << std::fixed << std::setprecision(2);

		if (freq >= 1e9) {
			ss << (freq / 1e9) << " GHz";
		} else if (freq >= 1e6) {
			ss << (freq / 1e6) << " MHz";
		} else if (freq >= 1e3) {
			ss << (freq / 1e3) << " kHz";
		} else {
			ss << freq << " Hz";
		}

		return ss.str();
	}

	// Every line gets the same "[h=... c=...]" counter prefix, applied here
	// once instead of by every call site's own format string/argument pair.
	template <typename... Args> inline void Println(std::format_string<Args...> fmt, Args&&... args) const {
		if (debugMode) [[unlikely]] {
			PrintlnSlow(fmt, std::forward<Args>(args)...);
		}
	}

  private:
	template <typename... Args>
	[[gnu::cold, gnu::noinline]] void PrintlnSlow(std::format_string<Args...> fmt, Args&&... args) const {
		std::print("[h={} c={}] ", halfCycles, cpuCycles);
		std::println(fmt, std::forward<Args>(args)...);
	}
};

} // namespace Sim