"""Serial transport -- the original (and still default) way to talk to a
real board over USB. Behavior-preserving extraction of what used to live
directly in `Controller`.
"""

import serial

from .base import Transport


class SerialTransport(Transport):
    def __init__(self, device: str, baudrate: int = 115200, timeout: float = 1.0):
        self.timeout = timeout
        self._serial = serial.Serial(device, baudrate, timeout=timeout)

    def write(self, data: bytes) -> None:
        self._serial.write(data)

    def readline(self, timeout: float | None = None) -> bytes | None:
        # pyserial's timeout is a persistent attribute, not a per-call
        # argument -- always reassert the effective timeout (falling back
        # to our own default) so a transient override (e.g. drain()'s
        # short wait_for) doesn't leak into later calls that pass None.
        effective = self.timeout if timeout is None else timeout
        if effective != self._serial.timeout:
            self._serial.timeout = effective
        line = self._serial.readline()
        return line if line else None

    def read(self, size: int, timeout: float | None = None) -> bytes | None:
        effective = self.timeout if timeout is None else timeout
        if effective != self._serial.timeout:
            self._serial.timeout = effective
        data = self._serial.read(size)
        return data if data else None

    def close(self) -> None:
        self._serial.close()
