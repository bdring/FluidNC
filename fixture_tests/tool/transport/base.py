"""Transport interface shared by all DUT connection kinds.

`Controller` (see `tool/controller.py`) talks only to this interface, so
`op_entries.py`'s fixture DSL is automatically transport-agnostic -- it was
already written against `Controller`'s API, not against pyserial directly.

Every transport speaks in whole lines with the trailing `\\r?\\n` stripped,
same as `pyserial.Serial.readline()` did for the original serial-only
`Controller`. `LineBufferedTransport` implements that line-splitting once,
on top of a `_fill()` primitive that each concrete transport provides to
pull in whatever raw bytes are available next (one WebSocket frame, one
`socket.recv()`, one chunk from a subprocess pipe, ...).
"""

import time


class Transport:
    """Abstract base. See module docstring."""

    #: Default timeout (seconds) used when a call doesn't override it.
    timeout = 1.0

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def readline(self, timeout: float | None = None) -> bytes | None:
        """Return the next complete line (no trailing newline), or None on timeout."""
        raise NotImplementedError

    def read(self, size: int, timeout: float | None = None) -> bytes | None:
        """Return up to `size` raw bytes (for XMODEM), or None on timeout."""
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError


class LineBufferedTransport(Transport):
    """Turns a byte-chunk source into `readline()`/`read()`.

    Concrete transports implement `_fill(timeout)` (pull in more raw bytes,
    or return b"" if none arrived within `timeout` seconds) and `write()`/
    `close()`. Everything else -- line splitting, the read-side of XMODEM,
    filtering out transport-level noise like WebSocket keepalives -- is
    handled here so it isn't duplicated per transport.
    """

    def __init__(self, timeout: float = 1.0):
        self.timeout = timeout
        self._buf = b""

    def _fill(self, timeout: float) -> bytes:
        """Pull in more raw bytes, blocking up to `timeout` seconds. b"" means none arrived."""
        raise NotImplementedError

    def _is_noise(self, line: bytes) -> bool:
        """Override to silently swallow transport-level bookkeeping lines
        that have no equivalent on serial (e.g. WebSocket's connection-id
        handshake), so fixture files stay identical across transports."""
        return False

    def readline(self, timeout: float | None = None) -> bytes | None:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            nl = self._buf.find(b"\n")
            if nl != -1:
                line, self._buf = self._buf[:nl], self._buf[nl + 1 :]
                line = line.rstrip(b"\r")
                if self._is_noise(line):
                    continue
                return line
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            chunk = self._fill(remaining)
            if chunk:
                self._buf += chunk

    def read(self, size: int, timeout: float | None = None) -> bytes | None:
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while len(self._buf) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            chunk = self._fill(remaining)
            if chunk:
                self._buf += chunk
        if not self._buf:
            return None
        data, self._buf = self._buf[:size], self._buf[size:]
        return data
