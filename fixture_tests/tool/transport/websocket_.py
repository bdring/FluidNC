"""WebSocket transport.

Talks to the same endpoint the browser WebUI uses: `ws://<host>[:port]/`.
Verified live against both a real board and the native `macos` simulator
build -- see fixture_tests/INTEGRATION_TEST_PLAN.md.

Two things a real WebUI client absorbs invisibly that a raw fixture
shouldn't have to know about:

- On connect, the server sends `currentID:N` then `CURRENT_ID:N` as text
  frames before anything else -- pure session bookkeeping, no serial
  equivalent.
- It sends an app-level text keepalive literally spelled `PING` from time
  to time (distinct from the WebSocket protocol's own ping/pong frames,
  which `websocket-client` already answers automatically).

Both are filtered out in `_is_noise()` so a fixture file written against
serial behaves the same way here.

One status report can arrive as a single frame containing more than one
line (e.g. the initial `<Idle|...>` status immediately followed by a
`[GC:...]` modal-state line) -- `LineBufferedTransport` already handles
splitting a multi-line chunk across successive `readline()` calls, so
that's not special-cased here.
"""

import websocket

from .base import LineBufferedTransport

_NOISE_PREFIXES = (b"currentID:", b"CURRENT_ID:")
_NOISE_LINES = (b"PING",)


class WebSocketTransport(LineBufferedTransport):
    def __init__(self, url: str, timeout: float = 1.0):
        super().__init__(timeout=timeout)
        self._ws = websocket.create_connection(url, timeout=timeout)

    def _is_noise(self, line: bytes) -> bool:
        return line in _NOISE_LINES or line.startswith(_NOISE_PREFIXES)

    def _fill(self, timeout: float) -> bytes:
        self._ws.settimeout(timeout)
        try:
            data = self._ws.recv()
        except (websocket.WebSocketTimeoutException, TimeoutError):
            return b""
        if data is None:
            return b""
        return data.encode("utf-8") if isinstance(data, str) else data

    def write(self, data: bytes) -> None:
        # Real-time single-byte commands (Ctrl-X, '?', ...) and regular
        # newline-terminated lines both travel the same way a browser
        # client would send them: as one text frame.
        self._ws.send(data.decode("utf-8", errors="replace"))

    def close(self) -> None:
        self._ws.close()
