"""Telnet transport.

Verified against real hardware (port 23): FluidNC's Telnet server sends no
banner and does no IAC option negotiation -- it's a plain text line
protocol identical in shape to serial, just over TCP. A raw `socket` is
enough; no `telnetlib`/negotiation handling needed.
"""

import socket

from .base import LineBufferedTransport

DEFAULT_PORT = 23


class TelnetTransport(LineBufferedTransport):
    def __init__(self, host: str, port: int = DEFAULT_PORT, timeout: float = 1.0):
        super().__init__(timeout=timeout)
        self._sock = socket.create_connection((host, port), timeout=timeout)

    def _fill(self, timeout: float) -> bytes:
        self._sock.settimeout(timeout)
        try:
            return self._sock.recv(4096)
        except (socket.timeout, TimeoutError):
            return b""

    def write(self, data: bytes) -> None:
        self._sock.sendall(data)

    def close(self) -> None:
        self._sock.close()
