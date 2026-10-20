#pragma once
#include "../../../../Simulator/Inspect.hpp"
#include "../../../../Simulator/Platform.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace SdCardCrc {
constexpr std::array<uint16_t, 256> MakeCrc16Table() {
	std::array<uint16_t, 256> t{};
	for (int i = 0; i < 256; i++) {
		uint16_t c = static_cast<uint16_t>(i << 8);
		for (int b = 0; b < 8; b++) c = (c & 0x8000) ? static_cast<uint16_t>((c << 1) ^ 0x1021) : static_cast<uint16_t>(c << 1);
		t[i] = c;
	}
	return t;
}
inline constexpr std::array<uint16_t, 256> kCrc16Table = MakeCrc16Table(); // CRC16-CCITT, poly 0x1021, init 0
} // namespace SdCardCrc

// SD / SDHC memory card in SPI mode (SPI mode 0, MSB first), backed by a file.
// The card is addressed in 512-byte blocks (SDHC style). Block data lives in
// the file, never in memory, and every written block reaches the file before
// the card answers. See SdCard.md.
class SdCard final : public Sim::Device, public Sim::IMemoryInspectable, public Sim::IRegisterInspectable {
  public:
	static constexpr uint32_t kBlockSize = 512;

	struct SdCardPins {
		Sim::Pin NotCS; // chip select (SPI /SS), input
		Sim::Pin SCLK;	// serial clock, input
		Sim::Pin MOSI;	// card data in, input
		Sim::Pin MISO;	// card data out, high-Z while deselected
	};
	SdCardPins Pins;

	// `path` is the backing file and `sizeBytes` the card capacity, a non-zero
	// multiple of 512. A missing file is created zero-filled, a shorter one is
	// extended with zeros, a longer one is rejected rather than truncated.
	SdCard(Sim::Platform* p, const std::string& path, uint64_t sizeBytes) :
		Pins{
			Sim::Pin::New(p, "SdCard.NotCS"),
			Sim::Pin::New(p, "SdCard.SCLK"),
			Sim::Pin::New(p, "SdCard.MOSI"),
			Sim::Pin::New(p, "SdCard.MISO")
		},
		_diagnostics(p->GetDiagnostics()), _blocks(sizeBytes / kBlockSize) {
		if (sizeBytes == 0 || sizeBytes % kBlockSize != 0)
			throw std::invalid_argument("SdCard: size must be a non-zero multiple of 512 bytes");

		namespace fs = std::filesystem;
		if (!fs::exists(path)) std::ofstream(path, std::ios::binary);
		const auto current = fs::file_size(path);
		if (current > sizeBytes)
			throw std::invalid_argument("SdCard: backing file '" + path + "' is larger than the card size");
		if (current < sizeBytes) fs::resize_file(path, sizeBytes);

		_file.open(path, std::ios::in | std::ios::out | std::ios::binary);
		if (!_file) throw std::runtime_error("SdCard: cannot open backing file '" + path + "'");

		Pins.NotCS.AddTrigger(this, Trig::Select, false);
		Pins.SCLK.AddTrigger(this, Trig::Clock, false);
	}

	uint32_t InspectableSize() const override {
		return static_cast<uint32_t>(std::min<uint64_t>(_blocks * kBlockSize, UINT32_MAX));
	}
	uint8_t Peek(uint32_t address) const override {
		char byte = 0;
		_file.clear();
		_file.seekg(address);
		_file.read(&byte, 1);
		return static_cast<uint8_t>(byte);
	}
	void Poke(uint32_t address, uint8_t value) override {
		_file.clear();
		_file.seekp(address);
		_file.put(static_cast<char>(value));
		_file.flush();
	}

	std::vector<std::pair<std::string, uint64_t>> DumpRegisters() const override {
		return {
			{ "Ready", _ready },
			{ "CrcOn", _crcOn },
			{ "LastCmd", _lastCmd },
			{ "LastArg", _lastArg },
			{ "LastR1", _lastR1 },
			{ "Blocks", _blocks }
		};
	}

	void Logic(Sim::TriggerMask) {
		const bool clk = Pins.SCLK.IsHigh();
		const bool rising = clk && !_lastClk;
		const bool falling = !clk && _lastClk;
		_lastClk = clk;

		// Level, not edge: a floating /CS reads as deselected.
		if (!Pins.NotCS.IsLow()) {
			if (_selected) Deselect();
			return;
		}
		if (!_selected) Select();

		if (rising) {
			_rx = static_cast<uint8_t>((_rx << 1) | (Pins.MOSI.IsHigh() ? 1 : 0));
			if (++_bit == 8) {
				_bit = 0;
				OnByte(_rx);
				_tx = NextTx();
			}
		} else if (falling) {
			DriveMiso();
		}
	}

  private:
	enum class Trig : uint8_t {
		Select = 0,
		Clock = 1
	};
	enum class RxMode : uint8_t {
		Command,
		WriteToken,
		WriteData
	};

	// R1 bits, data tokens and data response tokens.
	static constexpr uint8_t kIdle = 0x01, kIllegal = 0x04, kCrcError = 0x08, kParamError = 0x40;
	static constexpr uint8_t kTokenSingle = 0xFE, kTokenMultiWrite = 0xFC, kTokenStop = 0xFD;
	static constexpr uint8_t kRespAccepted = 0x05, kRespCrcError = 0x0B, kRespWriteError = 0x0D;
	static constexpr uint8_t kErrCardError = 0x01, kErrOutOfRange = 0x08;
	static constexpr int kBusyBytes = 2; // 0x00 bytes after a write or stop transmission

	static uint16_t Crc16(const uint8_t* data, size_t n) {
		uint16_t crc = 0;
		for (size_t i = 0; i < n; i++) crc = static_cast<uint16_t>((crc << 8) ^ SdCardCrc::kCrc16Table[(crc >> 8) ^ data[i]]);
		return crc;
	}
	static uint8_t Crc7(const uint8_t* data, size_t n) {
		uint8_t crc = 0;
		for (size_t i = 0; i < n; i++) {
			for (int b = 7; b >= 0; b--) {
				const bool feedback = ((crc >> 6) & 1) ^ ((data[i] >> b) & 1);
				crc = static_cast<uint8_t>((crc << 1) & 0x7F);
				if (feedback) crc ^= 0x09;
			}
		}
		return crc;
	}

	uint8_t IdleBit() const {
		return _ready ? 0 : kIdle;
	}

	void Select() {
		_selected = true;
		_tx = 0xFF;
		_bit = 0;
		DriveMiso();
	}

	void Deselect() {
		_selected = false;
		Pins.MISO.Write(Sim::LogicState::F); // high-Z
		_bit = 0;
		_cmdLen = 0;
		_rxMode = RxMode::Command;
		_streaming = false;
		_out.clear();
		_outPos = 0;
	}

	void DriveMiso() {
		Pins.MISO.Write(Sim::LogicState::ActiveHigh((_tx >> (7 - _bit)) & 1));
	}

	// Next byte to shift out: queued response data, the next block of a
	// multi-block read, or 0xFF.
	uint8_t NextTx() {
		if (_outPos >= _out.size()) {
			_out.clear();
			_outPos = 0;
			if (_streaming) QueueStreamBlock();
		}
		return _outPos < _out.size() ? _out[_outPos++] : 0xFF;
	}

	void OnByte(uint8_t b) {
		switch (_rxMode) {
		case RxMode::Command:
			if (_cmdLen == 0 && (b & 0xC0) != 0x40) return; // not a command start (idle 0xFF)
			_cmd[_cmdLen++] = b;
			if (_cmdLen == 6) {
				_cmdLen = 0;
				ExecuteCommand();
			}
			break;
		case RxMode::WriteToken:
			if (b == (_multiWrite ? kTokenMultiWrite : kTokenSingle)) {
				_wCount = 0;
				_rxMode = RxMode::WriteData;
			} else if (_multiWrite && b == kTokenStop) {
				_out.assign(kBusyBytes, 0x00);
				_outPos = 0;
				_rxMode = RxMode::Command;
			}
			break;
		case RxMode::WriteData:
			_wBuf[_wCount++] = b;
			if (_wCount == kBlockSize + 2) FinishWriteBlock();
			break;
		}
	}

	void FinishWriteBlock() {
		uint8_t response = kRespAccepted;
		const uint16_t sent = static_cast<uint16_t>((_wBuf[kBlockSize] << 8) | _wBuf[kBlockSize + 1]);
		if (_crcOn && sent != Crc16(_wBuf.data(), kBlockSize)) {
			response = kRespCrcError;
		} else if (_writeBlock >= _blocks || !WriteBlock(_writeBlock, _wBuf.data())) {
			response = kRespWriteError;
		}
		_diagnostics.Println("SdCard write block {} -> {:#04x}", _writeBlock, response);
		_writeBlock++;
		_out.assign(1, response);
		_out.insert(_out.end(), kBusyBytes, 0x00);
		_outPos = 0;
		_rxMode = (_multiWrite && response == kRespAccepted) ? RxMode::WriteToken : RxMode::Command;
	}

	bool ReadBlock(uint64_t block, uint8_t* dst) {
		_file.clear();
		_file.seekg(static_cast<std::streamoff>(block * kBlockSize));
		_file.read(reinterpret_cast<char*>(dst), kBlockSize);
		return _file.gcount() == kBlockSize;
	}
	bool WriteBlock(uint64_t block, const uint8_t* src) {
		_file.clear();
		_file.seekp(static_cast<std::streamoff>(block * kBlockSize));
		_file.write(reinterpret_cast<const char*>(src), kBlockSize);
		_file.flush();
		return static_cast<bool>(_file);
	}

	// Appends a read data packet: token, 512 data bytes, CRC16. A file error
	// appends an error token instead.
	void QueueBlock(uint64_t block) {
		const size_t base = _out.size();
		_out.resize(base + 1 + kBlockSize + 2);
		if (!ReadBlock(block, &_out[base + 1])) {
			_out.resize(base);
			_out.push_back(kErrCardError);
			_streaming = false;
			return;
		}
		_out[base] = kTokenSingle;
		const uint16_t crc = Crc16(&_out[base + 1], kBlockSize);
		_out[base + 1 + kBlockSize] = static_cast<uint8_t>(crc >> 8);
		_out[base + 2 + kBlockSize] = static_cast<uint8_t>(crc);
	}

	void QueueStreamBlock() {
		if (_nextBlock >= _blocks) {
			_out.push_back(kErrOutOfRange);
			_streaming = false;
			return;
		}
		QueueBlock(_nextBlock++);
	}

	// Appends a 16-byte register packet (CSD/CID) with its token and CRC16.
	void QueueRegister(const std::array<uint8_t, 16>& reg) {
		_out.push_back(kTokenSingle);
		_out.insert(_out.end(), reg.begin(), reg.end());
		const uint16_t crc = Crc16(reg.data(), reg.size());
		_out.push_back(static_cast<uint8_t>(crc >> 8));
		_out.push_back(static_cast<uint8_t>(crc));
	}

	std::array<uint8_t, 16> Csd() const {
		const uint32_t cSize = static_cast<uint32_t>(std::min<uint64_t>(std::max<uint64_t>(_blocks / 1024, 1) - 1, 0x3FFFFF));
		std::array<uint8_t, 16> csd = { 0x40, 0x0E, 0x00, 0x32, 0x5B, 0x59, 0x00,
			static_cast<uint8_t>(cSize >> 16), static_cast<uint8_t>(cSize >> 8), static_cast<uint8_t>(cSize),
			0x7F, 0x80, 0x0A, 0x40, 0x00, 0 };
		csd[15] = static_cast<uint8_t>((Crc7(csd.data(), 15) << 1) | 1);
		return csd;
	}

	std::array<uint8_t, 16> Cid() const {
		std::array<uint8_t, 16> cid = { 0x00, 'W', 'S', 'W', 'S', 'I', 'M', '0', 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x91, 0 };
		cid[15] = static_cast<uint8_t>((Crc7(cid.data(), 15) << 1) | 1);
		return cid;
	}

	// Starts a response: one 0xFF (command-to-response gap) then R1.
	void Reply(uint8_t r1) {
		_out.push_back(0xFF);
		_out.push_back(r1);
		_lastR1 = r1;
	}

	void ExecuteCommand() {
		const uint8_t index = _cmd[0] & 0x3F;
		const uint32_t arg = (uint32_t(_cmd[1]) << 24) | (uint32_t(_cmd[2]) << 16) | (uint32_t(_cmd[3]) << 8) | _cmd[4];
		const bool app = std::exchange(_appCmd, false);
		const bool wasStreaming = std::exchange(_streaming, false);
		_out.clear();
		_outPos = 0;
		_lastCmd = index;
		_lastArg = arg;

		// CRC7 is checked always for CMD0 and CMD8, otherwise only after CMD59 enabled it.
		if ((_crcOn || (!app && (index == 0 || index == 8))) && (_cmd[5] >> 1) != Crc7(_cmd.data(), 5)) {
			Reply(static_cast<uint8_t>(kCrcError | IdleBit()));
			_diagnostics.Println("SdCard CMD{} arg={:#x} bad CRC", index, arg);
			return;
		}

		if (app) {
			if (index == 41) {
				_ready = _initPolls++ >= 1; // first poll reports idle
				Reply(IdleBit());
			} else {
				Reply(static_cast<uint8_t>(kIllegal | IdleBit()));
			}
		} else {
			switch (index) {
			case 0:
				_ready = false;
				_crcOn = false;
				_appCmd = false;
				_initPolls = 0;
				Reply(kIdle);
				break;
			case 8:
				Reply(IdleBit());
				_out.insert(_out.end(), { 0x00, 0x00, static_cast<uint8_t>(_cmd[3] & 0x0F), _cmd[4] }); // R7: voltage range and check pattern echoed
				break;
			case 9:
				Reply(IdleBit());
				QueueRegister(Csd());
				break;
			case 10:
				Reply(IdleBit());
				QueueRegister(Cid());
				break;
			case 12:
				if (wasStreaming) {
					Reply(0x00);
					_out.insert(_out.end(), kBusyBytes, 0x00);
				} else {
					Reply(static_cast<uint8_t>(kIllegal | IdleBit()));
				}
				break;
			case 13:
				Reply(IdleBit());
				_out.push_back(0x00);
				break;
			case 16:
				Reply(IdleBit());
				break;
			case 17:
			case 18:
				if (!_ready) {
					Reply(static_cast<uint8_t>(kIllegal | kIdle));
				} else if (arg >= _blocks) {
					Reply(kParamError);
				} else {
					Reply(0x00);
					QueueBlock(arg);
					if (index == 18 && _out.back() != kErrCardError) {
						_streaming = true;
						_nextBlock = uint64_t(arg) + 1;
					}
				}
				break;
			case 24:
			case 25:
				if (!_ready) {
					Reply(static_cast<uint8_t>(kIllegal | kIdle));
				} else if (arg >= _blocks) {
					Reply(kParamError);
				} else {
					Reply(0x00);
					_writeBlock = arg;
					_multiWrite = index == 25;
					_rxMode = RxMode::WriteToken;
				}
				break;
			case 55:
				_appCmd = true;
				Reply(IdleBit());
				break;
			case 58:
				Reply(IdleBit());
				_out.insert(_out.end(), { static_cast<uint8_t>(_ready ? 0xC0 : 0x00), 0xFF, 0x80, 0x00 }); // R3: OCR, busy and CCS bits only once ready
				break;
			case 59:
				_crcOn = arg & 1;
				Reply(IdleBit());
				break;
			default:
				Reply(static_cast<uint8_t>(kIllegal | IdleBit()));
				break;
			}
		}
		_diagnostics.Println("SdCard {}CMD{} arg={:#x} -> R1={:#04x}", app ? "A" : "", index, arg, _lastR1);
	}

	Sim::Diagnostics& _diagnostics;
	mutable std::fstream _file;
	uint64_t _blocks;

	// Bit level.
	bool _lastClk = false;
	bool _selected = false;
	uint8_t _rx = 0;
	uint8_t _tx = 0xFF;
	uint8_t _bit = 0;

	// Byte level.
	RxMode _rxMode = RxMode::Command;
	std::array<uint8_t, 6> _cmd{};
	uint8_t _cmdLen = 0;
	std::vector<uint8_t> _out;
	size_t _outPos = 0;
	std::array<uint8_t, kBlockSize + 2> _wBuf{};
	uint32_t _wCount = 0;
	uint64_t _writeBlock = 0;
	bool _multiWrite = false;
	bool _streaming = false;
	uint64_t _nextBlock = 0;

	// Card state.
	bool _ready = false;
	bool _crcOn = false;
	bool _appCmd = false;
	int _initPolls = 0;
	uint8_t _lastCmd = 0xFF;
	uint32_t _lastArg = 0;
	uint8_t _lastR1 = 0;
};
