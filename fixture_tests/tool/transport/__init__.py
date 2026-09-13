"""DUT connection abstraction. See fixture_tests/INTEGRATION_TEST_PLAN.md
for the design this implements.

`create_transport()` parses a single "DUT spec" string so callers (like
`run_fixture` and pytest fixtures) don't need to know which concrete
transport class to import:

    /dev/cu.usbserial-0001    -> serial (bare path, backward compatible
                                  with run_fixture's original argument)
    serial:/dev/cu.usbserial-0001
    ws://192.168.4.67
    telnet://192.168.4.6[:port]      (default port 23)
    sim:macos                        (or "sim:linux") -- native simulator
                                      build, launched fresh per call
"""

from urllib.parse import urlparse

from .base import LineBufferedTransport, Transport
from .serial_ import SerialTransport
from .subprocess_ import SubprocessTransport, default_program_path
from .telnet_ import TelnetTransport
from .websocket_ import WebSocketTransport

__all__ = [
    "Transport",
    "LineBufferedTransport",
    "SerialTransport",
    "WebSocketTransport",
    "TelnetTransport",
    "SubprocessTransport",
    "default_program_path",
    "create_transport",
]


def create_transport(dut: str, *, baudrate: int = 115200, timeout: float = 1.0) -> Transport:
    if dut.startswith("sim:"):
        return SubprocessTransport(pio_env=dut[len("sim:") :], timeout=timeout)

    if dut.startswith("ws://") or dut.startswith("wss://"):
        return WebSocketTransport(dut, timeout=timeout)

    if dut.startswith("telnet://"):
        parsed = urlparse(dut)
        return TelnetTransport(parsed.hostname, parsed.port or 23, timeout=timeout)

    if dut.startswith("serial:"):
        return SerialTransport(dut[len("serial:") :], baudrate=baudrate, timeout=timeout)

    # Bare device path -- what run_fixture's `device` positional argument
    # has always accepted.
    return SerialTransport(dut, baudrate=baudrate, timeout=timeout)
