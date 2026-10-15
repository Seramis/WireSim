#include "TcpTerminal.hpp"
#include <chrono>
#include <iostream>
#include <sys/ioctl.h>

void TcpTerminal::RunServer() {
#ifdef _WIN32
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;
#endif

	socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == INVALID_SOCKET_HANDLE) return;

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(_port);

	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		CLOSE_SOCKET(server_fd);
		return;
	}

	listen(server_fd, 3);
	std::cout << _name << " listening on port " << _port << "..." << std::endl;

	while (!_stopRequested) {
		// Use select to wait for a connection with a timeout
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(server_fd, &read_fds);

		struct timeval timeout{0, 100000}; // 100ms timeout
		int activity = select((int)server_fd + 1, &read_fds, nullptr, nullptr, &timeout);

		if (activity <= 0 || !FD_ISSET(server_fd, &read_fds)) {
			continue; // Check _stopRequested and try again
		}

		socket_t client_fd = accept(server_fd, nullptr, nullptr);
		if (client_fd == INVALID_SOCKET_HANDLE) continue;

		// Disable Nagle's algorithm for terminal responsiveness
		opt = 1;
		setsockopt(client_fd, IPPROTO_TCP, 0x01 /* TCP_NODELAY */, (const char*)&opt, sizeof(opt));

		// Non-blocking: IsConnected()'s MSG_PEEK below relies on EWOULDBLOCK/EAGAIN
		// rather than blocking forever when the client has nothing more to send,
		// which is the state this socket is left in while paused (see PumpIncoming).
#ifdef _WIN32
		u_long nonBlocking = 1;
		ioctlsocket(client_fd, FIONBIO, &nonBlocking);
#else
		int nonBlocking = 1;
		ioctl(client_fd, FIONBIO, &nonBlocking);
#endif

		// Send initial greeting
		std::string greeting = "Connected to " + _name + " on port " + std::to_string(_port) + "!\n";
		bool previousWasCarriageReturn = false;
		for (uint8_t b : greeting)
			DeliverToStream(client_fd, b, previousWasCarriageReturn);

		// Inner loop: Handle the active client
		while (!_stopRequested) {
			FlushBacklog(client_fd, previousWasCarriageReturn);
			if (PumpIncoming(client_fd) == PumpResult::Disconnected) break;
		}
		CLOSE_SOCKET(client_fd);
	}

	CLOSE_SOCKET(server_fd);
#ifdef _WIN32
	WSACleanup();
#endif
}

bool TcpTerminal::IsConnected(socket_t client_fd) {
	char buffer;
	int res = recv(client_fd, &buffer, 1, MSG_PEEK);
	if (res == 0) return false; // Graceful close
	if (res < 0) {
#ifdef _WIN32
		return WSAGetLastError() == WSAEWOULDBLOCK;
#else
		return errno == EWOULDBLOCK || errno == EAGAIN;
#endif
	}
	return true;
}

// Machine -> host. The host always gets CRLF for a new line. With the default LF
// machine newline, '\r' is dropped, so a machine sending CRLF still yields one
// line break. With a CR machine newline, '\r' ends the line and a following '\n'
// is absorbed; a lone '\n' still ends it.
void TcpTerminal::AppendTranslated(std::string& out, uint8_t b, bool& previousWasCarriageReturn) {
	if (_machineNewline == '\n') {
		if (b == '\n') out += "\r\n";
		else if (b != '\r') out += (char)b;
		return;
	}
	if (b == '\r') {
		out += "\r\n";
		previousWasCarriageReturn = true;
	} else if (b == '\n') {
		if (!previousWasCarriageReturn) out += "\r\n";
		previousWasCarriageReturn = false;
	} else {
		// Forward everything else, including 0x1B (Escape)
		out += (char)b;
		previousWasCarriageReturn = false;
	}
}

void TcpTerminal::DeliverToStream(socket_t client_fd, uint8_t b, bool& previousWasCarriageReturn) {
	std::string out;
	AppendTranslated(out, b, previousWasCarriageReturn);
	if (!out.empty()) send(client_fd, out.data(), (int)out.size(), 0);
}

// Drains _backlog under its lock into a local buffer, then translates and sends it
// outside the lock: a big burst can mean tens of thousands of queued bytes, and
// holding _backlogMutex across a send() syscall per byte would block Receive()/RX()
// (the producer side) for the whole drain. Sending the translated buffer in as few
// send() calls as the socket needs also keeps syscall count to O(1) per drain pass.
void TcpTerminal::FlushBacklog(socket_t client_fd, bool& previousWasCarriageReturn) {
	std::vector<uint8_t> drained;
	{
		std::lock_guard<std::mutex> lock(_backlogMutex);
		drained.reserve(_backlog.size());
		while (!_backlog.empty()) {
			drained.push_back(_backlog.front());
			_backlog.pop();
		}
	}
	if (drained.empty()) return;

	std::string out;
	out.reserve(drained.size());
	for (uint8_t b : drained)
		AppendTranslated(out, b, previousWasCarriageReturn);

	size_t sent = 0;
	while (sent < out.size()) {
		int n = send(client_fd, out.data() + sent, (int)(out.size() - sent), 0);
		if (n <= 0) break; // disconnect/error -- IsConnected() will notice
		sent += (size_t)n;
	}
}

// Pulls at most one byte off the socket and relays it via Send(), but only while the
// UART's RTS says it has room and our own relay queue isn't already backed up (bytes
// read off the wire here are committed -- they'll be delivered even if CTS drops a
// moment later, so this terminal must stop reading just as early as RX() stops
// accepting). Leaving the rest unread in the kernel socket buffer lets TCP's own
// window apply backpressure to the real sender -- nothing is dropped, it just waits.
TcpTerminal::PumpResult TcpTerminal::PumpIncoming(socket_t client_fd) {
	if (!(_clearToSend && PendingOutgoing() < MAX_PENDING)) {
		if (!IsConnected(client_fd)) return PumpResult::Disconnected; // notice a disconnect while paused
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
		return PumpResult::Idle;
	}

	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(client_fd, &read_fds);
	struct timeval timeout{0, 10000}; // 10ms
	if (select((int)client_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) return PumpResult::Idle;

	uint8_t b;
	int bytesRead = recv(client_fd, (char*)&b, 1, 0);
	if (bytesRead <= 0) {
		// A negative result while still connected is a spurious wakeup, not a disconnect.
		return (bytesRead < 0 && IsConnected(client_fd)) ? PumpResult::Idle : PumpResult::Disconnected;
	}

	// Host -> machine. '\r' is ignored, so CRLF and LF both arrive as one line end.
	if (b == '\r') return PumpResult::Idle;
	if (b == '\n') b = _machineNewline;
	if (b == 0x7F) b = 0x08; // DEL to Backspace
	Send(b);
	return PumpResult::Idle;
}