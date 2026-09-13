"""Subprocess transport -- drives a native `macos`/`linux` build of FluidNC
(FluidNC/capture/main.cpp + FluidNC/posix/Console.cpp) over its own
stdin/stdout, exactly like talking to a board over serial.

Process lifecycle notes (see fixture_tests/INTEGRATION_TEST_PLAN.md):

- The binary is launched with `-` so it keeps running instead of exiting
  once any `-c`-queued commands (none, here) drain -- see
  `FluidNC/capture/main.cpp`.
- Its local flash filesystem is a directory literally named
  `native_localfs`, resolved *relative to the process's cwd*
  (`FluidNC/capture/localfs.cpp`), and it always looks for `config.yaml`
  inside that directory -- there's no command-line flag for it. So each
  instance gets its own temp working directory with its own
  `native_localfs/config.yaml`, rather than sharing (and fighting over)
  the repo's own `native_localfs/`.
- Known gap, deliberately not worked around here: the repo's
  `native_localfs/config.yaml` is a real-hardware config, not the
  simulator-friendly `native_localfs/sim.yaml`. `SubprocessTransport`
  defaults to copying `sim.yaml` in as `config.yaml` for exactly this
  reason, but a caller can pass an explicit `config_path` to use anything
  else (including the real one, if a test wants to exercise config
  validation/error handling itself).
"""

import queue
import shutil
import subprocess
import tempfile
import threading
from pathlib import Path

from .base import LineBufferedTransport

# fixture_tests/tool/transport/subprocess_.py -> repo root
_REPO_ROOT = Path(__file__).resolve().parents[3]


def default_program_path(pio_env: str) -> Path:
    """`pio_env` is a native PlatformIO environment name, e.g. "macos" or
    "linux" -- see the `[env:macos]`/`[env:linux]` sections of
    platformio.ini. Does not build it; that's the caller's job (once per
    test session, not once per test)."""
    return _REPO_ROOT / ".pio" / "build" / pio_env / "program"


class SubprocessTransport(LineBufferedTransport):
    def __init__(
        self,
        pio_env: str = "macos",
        *,
        program_path: Path | None = None,
        config_path: Path | None = None,
        timeout: float = 1.0,
        startup_timeout: float = 10.0,
    ):
        super().__init__(timeout=timeout)

        program_path = program_path or default_program_path(pio_env)
        if not program_path.exists():
            raise FileNotFoundError(
                f"{program_path} does not exist -- build it first, e.g. `pio run -e {pio_env}`"
            )

        self._workdir = Path(tempfile.mkdtemp(prefix="fluidnc_sim_"))
        localfs = self._workdir / "native_localfs"
        localfs.mkdir()

        if config_path is None:
            default_sim_config = _REPO_ROOT / "native_localfs" / "sim.yaml"
            if default_sim_config.exists():
                config_path = default_sim_config
        if config_path is not None:
            shutil.copy(config_path, localfs / "config.yaml")

        self._proc = subprocess.Popen(
            [str(program_path), "-"],
            cwd=self._workdir,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

        self._queue: queue.Queue[bytes | None] = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

        self._wait_for_banner(startup_timeout)

    def _read_loop(self) -> None:
        # read1() (rather than read()) returns as soon as *some* data is
        # available instead of waiting to fill a full buffer, so this
        # forwards output promptly.
        stdout = self._proc.stdout
        assert stdout is not None
        try:
            while True:
                chunk = stdout.read1(4096)
                if not chunk:
                    break
                self._queue.put(chunk)
        finally:
            self._queue.put(None)  # EOF sentinel

    def _fill(self, timeout: float) -> bytes:
        try:
            chunk = self._queue.get(timeout=timeout)
        except queue.Empty:
            return b""
        return chunk or b""

    def _wait_for_banner(self, startup_timeout: float) -> None:
        """Block until the "Grbl " startup banner appears, the same signal
        `Controller.send_soft_reset()` waits for after a reset."""
        line = self.readline(timeout=startup_timeout)
        while line is not None and not line.startswith(b"Grbl "):
            line = self.readline(timeout=startup_timeout)
        if line is None:
            raise TimeoutError(
                f"Timed out after {startup_timeout}s waiting for the simulator's startup banner "
                f"(workdir={self._workdir})"
            )

    def write(self, data: bytes) -> None:
        assert self._proc.stdin is not None
        self._proc.stdin.write(data)
        self._proc.stdin.flush()

    def close(self) -> None:
        try:
            if self._proc.stdin is not None:
                self._proc.stdin.close()
            self._proc.wait(timeout=2)
        except Exception:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=2)
            except Exception:
                self._proc.kill()
        finally:
            shutil.rmtree(self._workdir, ignore_errors=True)
