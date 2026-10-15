#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class Terminal {
  public:
	// Cap on each relay queue below (RX's _incoming, and a peer's own outgoing
	// backlog -- see TcpTerminal::RunServer). Bytes already queued past a device's
	// own FIFO-full point are committed and will be delivered regardless of a later
	// CTS change, so this must stay small: it's the slack an RTS/CTS deassertion
	// has to still be honored before the real damage (an overrun) is done.
	static constexpr size_t MAX_PENDING = 2;

	// Cap on _outgoing, independent of MAX_PENDING above: a producer (e.g. the CPU
	// via WriteTHR) can queue bytes far faster than TxThread relays them to
	// listeners. Dropping the oldest byte once this fills keeps that queue -- and
	// the lock/dispatch work of draining it -- bounded regardless of burst size.
	static constexpr size_t MAX_OUTGOING_PENDING = 4096;

  private:
	std::vector<std::function<void(uint8_t)>> _listeners;
	std::mutex _listenerMutex;

	std::vector<std::function<void(bool)>> _rtsListeners;
	std::mutex _rtsListenerMutex;

	std::queue<uint8_t> _incoming;
	std::queue<uint8_t> _outgoing;
	std::mutex _queueMutex;
	// Signalled on every push/pop to either queue and on shutdown, so TxThread,
	// RxThread, and a blocked RX() wake immediately instead of polling.
	std::condition_variable _queueCv;

	// TX-dispatch (_outgoing -> listeners) and RX-drain (_incoming -> Receive()) run
	// on separate threads. RX() below can block a peer's TX-dispatch thread waiting
	// for room; if both directions shared one loop, that block would also stop this
	// terminal from draining its own _incoming, and a peer blocked the same way
	// would deadlock against it. Separate threads mean a block on one direction
	// never starves the other.
	std::thread _txThread, _rxThread;
	std::atomic<bool> _running{true};

	// Pops the front of `queue` into `out` under `_queueMutex`, or returns false if
	// it was empty.
	bool TryPop(std::queue<uint8_t>& queue, uint8_t& out) {
		std::lock_guard<std::mutex> lock(_queueMutex);
		if (queue.empty()) return false;
		out = queue.front();
		queue.pop();
		return true;
	}

	// Pushes `b` onto `queue` under `_queueMutex`, dropping the oldest entry first
	// if it's already at `cap`. Unlike a bounded push, this never fails to accept `b`.
	void PushDroppingOldest(std::queue<uint8_t>& queue, uint8_t b, size_t cap) {
		{
			std::lock_guard<std::mutex> lock(_queueMutex);
			while (queue.size() >= cap)
				queue.pop();
			queue.push(b);
		}
		_queueCv.notify_all();
	}

	void TxThread() {
		while (_running) {
			uint8_t b;
			if (TryPop(_outgoing, b)) {
				_queueCv.notify_all(); // freed a slot for a producer waiting on _outgoing room
				std::lock_guard<std::mutex> lock(_listenerMutex);
				for (auto& l : _listeners)
					l(b);
			} else {
				std::unique_lock<std::mutex> lock(_queueMutex);
				_queueCv.wait(lock, [this] {
					return !_running || !_outgoing.empty();
				});
			}
		}
	}

	void RxThread() {
		while (_running) {
			uint8_t b;
			if (TryPop(_incoming, b)) {
				_queueCv.notify_all(); // freed a slot for RX() waiting on _incoming room
				Receive(b);
			} else {
				std::unique_lock<std::mutex> lock(_queueMutex);
				_queueCv.wait(lock, [this] {
					return !_running || !_incoming.empty();
				});
			}
		}
	}

  protected:
	virtual void Receive(uint8_t b) = 0;
	void Send(uint8_t b) {
		PushDroppingOldest(_outgoing, b, MAX_OUTGOING_PENDING);
	}

	// Announce this terminal's own RTS state changing (e.g. a UART mirroring its MCR
	// RTS bit). Fires listeners registered via AddRTS, which NullModemConnect wires to
	// the peer's CTS().
	void UpdateRTS(bool ready) {
		std::lock_guard<std::mutex> lock(_rtsListenerMutex);
		for (auto& l : _rtsListeners)
			l(ready);
	}

	// The peer's RTS state, delivered to this terminal's CTS input. Default: ignored.
	virtual void CTS([[maybe_unused]] bool clearToSend) {}

	// How many bytes this terminal has already accepted but not yet handed to its
	// TX listener(s). A producer feeding this terminal (see TcpTerminal::RunServer)
	// should stop reading more from its own source once this is at MAX_PENDING,
	// the same way RX() below stops accepting -- otherwise bytes already read off
	// the wire pile up here instead, defeating the point of checking CTS at all.
	size_t PendingOutgoing() {
		std::lock_guard<std::mutex> lock(_queueMutex);
		return _outgoing.size();
	}

  public:
	Terminal() {
		_txThread = std::thread(&Terminal::TxThread, this);
		_rxThread = std::thread(&Terminal::RxThread, this);
	}

	virtual ~Terminal() {
		_running = false;
		_queueCv.notify_all();
		if (_txThread.joinable()) _txThread.join();
		if (_rxThread.joinable()) _rxThread.join();
	}

	// May block (bounded by MAX_PENDING, drained promptly by this terminal's own
	// worker thread, woken via _queueCv rather than polled). Only ever called from
	// a plain thread -- another Terminal's worker thread dispatching a TX listener
	// -- never from the sim thread, so blocking here is safe. Never call this from
	// Device::Logic().
	virtual void RX(uint8_t b) {
		std::unique_lock<std::mutex> lock(_queueMutex);
		_queueCv.wait(lock, [this] {
			return !_running || _incoming.size() < MAX_PENDING;
		});
		if (!_running) return;
		_incoming.push(b);
		lock.unlock();
		_queueCv.notify_all();
	}

	void AddTX(std::function<void(uint8_t)> listener) {
		std::lock_guard<std::mutex> lock(_listenerMutex);
		_listeners.push_back(listener);
	}

	bool RemoveTX([[maybe_unused]] std::function<void(uint8_t)> listener) {
		// std::function has no equality comparison, so a listener registered via
		// AddTX can't be identified here to remove it; this is a no-op stub.
		return false;
	}

	void AddRTS(std::function<void(bool)> listener) {
		std::lock_guard<std::mutex> lock(_rtsListenerMutex);
		_rtsListeners.push_back(listener);
	}

	static void NullModemConnect(Terminal* t1, Terminal* t2) {
		t1->AddTX([t2](uint8_t b) {
			t2->RX(b);
		});
		t2->AddTX([t1](uint8_t b) {
			t1->RX(b);
		});
		t1->AddRTS([t2](bool ready) {
			t2->CTS(ready);
		});
		t2->AddRTS([t1](bool ready) {
			t1->CTS(ready);
		});
	}
};
