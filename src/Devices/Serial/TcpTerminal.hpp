#pragma once
#include "Terminal.hpp"
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define INVALID_SOCKET_HANDLE INVALID_SOCKET
#define CLOSE_SOCKET closesocket
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET_HANDLE -1
#define CLOSE_SOCKET close
#endif

class TcpTerminal : public Terminal {
  private:
	std::string _name;
	int _port;
	int _backlogSize;

	// The byte the attached machine uses to end a line ('\n', or '\r' for firmware
	// that predates the LF convention). The host side is always CRLF out, and LF or
	// CRLF in; see AppendTranslated() and PumpIncoming().
	uint8_t _machineNewline;

	std::queue<uint8_t> _backlog;
	std::mutex _backlogMutex;

	std::thread _serverThread;
	std::atomic<bool> _stopRequested{false};

	// Mirrors the connected UART's RTS output (this terminal's CTS input). Starts
	// deasserted, matching the UART's MCR reset state -- the real-world default is
	// "wait until the other end says it's ready", not "assume it is".
	std::atomic<bool> _clearToSend{false};

	enum class PumpResult {
		Idle,
		Disconnected
	};

	void RunServer();
	bool IsConnected(socket_t client_fd);
	void DeliverToStream(socket_t client_fd, uint8_t b, bool& previousWasCarriageReturn);
	void AppendTranslated(std::string& out, uint8_t b, bool& previousWasCarriageReturn);
	void FlushBacklog(socket_t client_fd, bool& previousWasCarriageReturn);
	PumpResult PumpIncoming(socket_t client_fd);

  protected:
	void Receive(uint8_t b) override {
		std::lock_guard<std::mutex> lock(_backlogMutex);
		while (_backlog.size() >= (size_t)_backlogSize) {
			_backlog.pop();
		}
		_backlog.push(b);
	}

	void CTS(bool clearToSend) override {
		_clearToSend = clearToSend;
	}

  public:
	// Writes straight into `_backlog` instead of going through Terminal::RX()'s
	// MAX_PENDING-bounded relay queue. That bound exists to make an inbound FIFO
	// honor RTS/CTS promptly (see Terminal.hpp); this terminal is a display/output
	// sink with no RX-side flow control of its own to honor, so none of that
	// backpressure applies here. `_backlog` is itself already a bounded,
	// drop-oldest queue, so nothing is lost by writing to it directly.
	void RX(uint8_t b) override {
		Receive(b);
	}

	TcpTerminal(std::string name = "TcpTerminal", int port = 8080, int backlogSize = 2048, uint8_t machineNewline = '\n') :
		_name(name), _port(port), _backlogSize(backlogSize), _machineNewline(machineNewline) {}

	~TcpTerminal() {
		Stop();
	}

	void Start() {
		_stopRequested = false;
		_serverThread = std::thread(&TcpTerminal::RunServer, this);
	}

	void Stop() {
		_stopRequested = true;
		if (_serverThread.joinable()) {
			_serverThread.join();
		}
	}
};