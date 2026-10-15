# TcpTerminal

`Terminal` (see `Terminal.hpp`) bridge device byte stream to one TCP client: device output go socket, bytes read socket go device. Host-side transport, not `Sim::Device`. No pins, run own threads, so `Logic()` never touch it.

## Using it

```cpp
TcpTerminal console{"Console", 20022};          // name (greeting text), port, optional backlog size (default 2048)
Terminal::NullModemConnect(&console, &uart);    // uart is any Terminal-derived device
// ... after Platform::Start() has been arranged:
console.Start();                                // opens the listening socket on its own thread
// ... on shutdown:
console.Stop();                                 // the destructor also calls it
```

Connect with `socat - TCP:localhost:20022` or `connect.sh 20022`. Script can use any socket client (see "Driving it from a script").

- **Port in use:** if `bind` fail, server thread return silent, simulation run no console. "listening on port N" line on stdout only sign it start.
- **One client at a time.** Second client not accept till first disconnect. Bytes device send while nobody connect (up to backlog size, oldest drop first) deliver on next connect. Anything old client send that device not yet consume, lost.
- **Greeting:** every new client first get `Connected to <name> on port <port>!`. Come from `TcpTerminal`, not device.

## Input is gated on the device's RTS

Socket read only while terminal's `_clearToSend` true, and it **start false**. Follow connected device RTS: device call `UpdateRTS(ready)` and `NullModemConnect` route that to this terminal `CTS()`. Input held till device announce RTS assert. Device keep RTS deassert till firmware init it, so char typed earlier wait in socket.

Every terminal chip must take part: either drive RTS for real (from register bit, deassert while receive buffer full) or announce constant `true`. Device that never call `UpdateRTS` leave gate shut, nothing typed get forward. For device author:

- `UpdateRTS` reach only listener register at that moment, so announce in device constructor, before `NullModemConnect`, lost. Announce again on first `Logic()` call or bus access after wiring.
- Constant `true` right for device with no flow-control signal and receive path that cannot overrun.

Read also pause while relay queue hold `Terminal::MAX_PENDING` (2) bytes. Together two condition let TCP own window give backpressure: bytes device not ready for stay in kernel socket buffer instead of drop, so sender can write full speed.

## Translation

| Direction | Rule |
| --- | --- |
| Device to client | `\r` become `\r\n`; lone `\n` become `\r\n`; `\n` of `\r\n` pair not double. Everything else, include `ESC`, pass through. |
| Client to device | `0x7F` (DEL) become `0x08` (backspace). Nothing else change, so send Enter as `\r`. |

Device that echo `\r` then print own `\r\n` show blank line between echo and reply. That expected, not duplicate.

## Driving it from a script

Link need no pacing: whole line in one `send()`, several line in one `send()`, one `send()` per byte with no delay, all arrive intact, with or without `TCP_NODELAY`. What script must respect is firmware, not link:

- Terminate line with `\r`.
- Wait for reply go quiet (firmware prompt) before send next line. Program that poll keyboard while run, like BASIC do look for Ctrl-C, consume bytes sent ahead of prompt. Example: send `NEW` together with program line `10 FOR…` lose `1` of line 10 to that poll; send line after `OK` stay intact.

```python
import socket, time
s = socket.create_connection(("localhost", 20022))
s.setblocking(False)

def send(text):                    # a whole line in one send() is fine
    s.sendall(text.encode("latin1"))

def read_until_quiet(quiet=0.5):
    buf, last = b"", time.time()
    while time.time() - last < quiet:
        try:
            d = s.recv(4096)
            if d: buf += d; last = time.time()
        except BlockingIOError:
            time.sleep(0.01)
    return buf.decode("latin1")
```

Read greeting with `read_until_quiet()` first, then `send("...\r")` follow by `read_until_quiet()` for each line.

## Threads

`Start()` launch accept/relay thread. Base class run TX-dispatch thread and RX-drain thread. `RX()` (call by peer TX thread) write straight into output backlog, never block. `Receive()` and `CTS()` may run on any these thread, so mutex guard output backlog and atomic hold `_clearToSend`.