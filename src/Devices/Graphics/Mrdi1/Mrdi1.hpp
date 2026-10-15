#pragma once
#include "../../../Simulator/Platform.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Monochromatic Raster Display Interface (MRDI-1): a 320x200 1-bit
// framebuffer controller. See Mrdi1.md alongside this file for the
// register/opcode protocol, the triggering rationale, and the
// render-thread/SDL threading model.
class Mrdi1 final : public Sim::Device {
  public:
	static constexpr int Width = 320;
	static constexpr int Height = 200;
	static constexpr int VramSize = (Width * Height) / 8; // 8000 bytes

	// Text layer: a separate 40x25 character grid overlaid in front of the
	// pixel layer. CharWidth/CharHeight both divide Width/Height exactly.
	// See Mrdi1.md for the WRITE_TEXT/TEXT_ATTR protocol and compositing.
	static constexpr int CharWidth = 8;
	static constexpr int CharHeight = 8;
	static constexpr int TextCols = Width / CharWidth;	 // 40
	static constexpr int TextRows = Height / CharHeight; // 25
	static constexpr int TextSize = TextCols * TextRows; // 1000

	struct Mrdi1Pins {
		Sim::Bus D;
		Sim::Pin A0, NotCE, NotWE;
	};
	Mrdi1Pins Pins;

	Mrdi1(Sim::Platform* p, std::string name = "MRDI1");
	~Mrdi1();

	void Logic(Sim::TriggerMask mask);

	// Opens the SDL window if it isn't already open; no-op otherwise.
	// Safe to call from any thread (does not touch Pins/nets). See
	// Mrdi1.md's "Threading model" for why the window starts closed.
	void OpenWindow();

  private:
	// Stable-only trigger; see Mrdi1.md for why (non-idempotent commit).
	enum class CTRL : uint8_t {
		NotWE = 0
	};
	enum class FsmState : uint8_t {
		ExpectAddrLow,
		ExpectAddrHigh,
		Streaming
	};

	// IR opcodes; mirrors MRDI1::OP in constants.inc.
	static constexpr uint8_t OpDrawPixels = 0x00;
	static constexpr uint8_t OpWriteText = 0x01;
	static constexpr uint8_t OpTextAttr = 0x02;

	// Attribute bits (TEXT_ATTR), mirrors MRDI1::ATTR in constants.inc.
	static constexpr uint8_t AttrInvert = 1 << 0;

	// The addr-lo/addr-hi phases and the auto-incrementing pointer are
	// shared across opcodes (only one stream is ever active at a time);
	// the modulus and target buffer for the current stream depend on
	// _opcode. See CommitWrite.
	static uint16_t BufferSizeFor(uint8_t opcode);

	Sim::Diagnostics& _diagnostics;
	std::string _name;

	bool _lastIsWrite = false;
	bool _lastA0 = false;
	uint8_t _lastData = 0;

	uint8_t _opcode = 0;
	uint16_t _vramPtr = 0;
	FsmState _state = FsmState::ExpectAddrLow;

	std::vector<uint8_t> _vram;
	std::vector<uint8_t> _textVram;
	std::vector<uint8_t> _textAttr;
	std::mutex _vramMutex; // guards _vram, _textVram and _textAttr alike
	std::atomic<bool> _vramDirty{true};

	std::thread _renderThread;
	std::atomic<bool> _stopRequested{false};
	std::atomic<bool> _windowOpen{false};
	bool _openRequested = false;
	std::mutex _openMutex;
	std::condition_variable _openCv;

	void CommitWrite(bool a0, uint8_t data);
	void RenderLoop();
};
