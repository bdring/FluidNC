# Plan: transport-agnostic integration test scheme

Goal: one test suite, several kinds of tests, that can run against either
the native `macos`/`linux` simulator build (fast, no hardware, good for CI
and for testing anything that isn't machine-specific) or a real board
(serial, WebSocket, or Telnet -- for motion/GPIO/homing/limits and anything
else that genuinely needs hardware).

This builds directly on `fixture_tests/`, which already has most of the
pieces -- it's just hardwired to serial only. Nothing here is committed
yet; this is a design for review before I touch working code.

## What's already there (inventory)

- **`fixture_tests/tool/op_entries.py`** -- a small line-oriented DSL
  (`->`, `<-`, `<~`, `<|`, `<...`, `=>`) for scripting a command/response
  conversation. This layer is already transport-agnostic: it only calls
  `controller.send_line()` / `.current_line()` / `.next_line()` /
  `.getc()` / `.putc()`. Nothing here needs to change.
- **`fixture_tests/tool/controller.py`** -- the one place that's hardwired
  to `pyserial`. This is the seam to generalize.
- **`fixture_tests/run_fixture`** -- CLI entry point: `run_fixture <device>
  <fixture-file-or-dir>`. Always does a soft-reset (`Ctrl-X`) + waits for
  the `Grbl ` banner before each fixture.
- **Native builds** (`env:macos`, `env:linux` in `platformio.ini`) --
  `FluidNC/capture/main.cpp` builds a real POSIX binary of FluidNC. It
  reads G-code from real stdin/stdout (`FluidNC/posix/Console.cpp`), *and*
  simultaneously starts the normal WebSocket server (port 8080 by default)
  and Telnet server (port 8123), because it's the same `WebUIServer.cpp`
  used on real hardware. So the simulator is reachable three ways at once:
  stdio, WebSocket, Telnet.
  - `program -c "G0 X10"` queues a command; `-` means "keep running
    afterward" instead of exiting once queued commands drain
    (`FluidNC/capture/main.cpp`, `FluidNC/posix/Console.cpp`'s
    `_exit_after_cmds`). With no `-c` and no `-`, it just runs normally
    reading stdin, same as talking to a board over serial.
  - Local flash filesystem is a directory literally named `native_localfs`
    *relative to the process's cwd* (`FluidNC/capture/localfs.cpp:21`,
    `LocalFS.prefix = "native_localfs"`), and it looks for `config.yaml`
    inside that directory. That means **cwd selects the config** -- no
    command-line flag for it.
- **Existing sim-friendly configs**: `native_localfs/sim.yaml` and
  `native_localfs/simtimed.yaml` already use `engine: simulator` /
  `engine: timed` with fake `sim.N` pins (no real GPIO needed). These look
  like exactly what a simulator-driven test should boot with.
- **WebSocket protocol** (verified live against both a real board and the
  macos build): on connect the server sends `currentID:N` and
  `CURRENT_ID:N` as **text** frames, then status reports (`<Idle|...>`,
  `[GC:...]`) as **binary** frames, and unsolicited `[MSG:...]` /
  keepalive `PING` as text. A client just sends plain text frames (e.g.
  `?`, `$X`, `$J=...`) -- no separate command framing.
- **Telnet** (real board, port 23): works exactly like serial -- no
  banner, plain text in/out, `?` gets a normal status report back.

## What's missing / found during this spike (not fixed, flagging only)

1. **`native_localfs/config.yaml` is a real-hardware config**, not
   `sim.yaml`. Since the loader always reads literally `config.yaml`,
   running the simulator today from the repo's own `native_localfs/`
   lands in a permanent `Configuration is invalid` alarm (real GPIO pins
   like `gpio.26`/`gpio.22`/`gpio.23` don't exist in the simulator). You
   said not to worry about this yet -- noted here only because the test
   harness needs to *not* depend on that shared file (see "per-test
   working directory" below), which sidesteps it entirely regardless of
   whether/how it's eventually fixed.
2. **Telnet on the native `macos` build (port 8123) didn't respond** to a
   plain `?` over a raw socket, while the identical test against the real
   board's Telnet (port 23) worked fine, and stdio/WebSocket both worked
   fine against the same simulator process. Unconfirmed whether this is a
   real gap in the native/capture Telnet backend or something about the
   Alarm/config-invalid state from #1. Needs a follow-up spike once #1 is
   sorted, before any Telnet-transport tests can target the simulator.
   Real-hardware Telnet tests are unaffected.
3. `fixture_tests/requirements.txt` has no WebSocket client library yet.
   `websocket-client` is already installed in this environment and is a
   reasonable, simple choice (synchronous, no asyncio needed to match the
   existing synchronous `Controller` design).

## Proposed architecture

### 1. `Transport` interface

Replace `Controller`'s direct `pyserial` calls with a small transport
interface, and make `Controller` take any object implementing it:

```python
class Transport(Protocol):
    def write(self, data: bytes) -> None: ...
    def readline(self, timeout: float) -> bytes | None: ...   # None on timeout
    def read(self, size: int) -> bytes | None: ...             # for XMODEM getc
    def close(self) -> None: ...
```

`Controller` keeps its current public API (`send_line`, `current_line`,
`next_line`, `clear_line`, `getc`, `putc`, `drain`, `send_soft_reset`)
unchanged -- callers (`run_fixture`, `op_entries.py`) don't need to know
which transport they're on.

### 2. Concrete transports

- **`SerialTransport`** -- today's `pyserial` code, extracted as-is.
- **`WebSocketTransport`** -- `websocket-client`, connecting to
  `ws://<host>/`. Handles the `currentID`/`CURRENT_ID` handshake
  transparently in `__init__` (consume and discard, like a real WebUI
  client would) so fixtures don't need to know about it. Treats each
  frame (binary or text) as one "line" -- matches observed behavior where
  the server already sends complete report lines per frame.
- **`TelnetTransport`** -- raw `socket`, no telnet IAC negotiation needed
  (confirmed against real hardware above -- it's just plain text over
  TCP). Simplest of the three network transports.
- **`SubprocessTransport`** -- launches the native `program` binary
  (`.pio/build/macos/program` or `.../linux/program`) with `-` (stay
  running), talks over its stdin/stdout pipes. This is the one that needs
  the most care around the process lifecycle (see below).

### 3. Simulator process lifecycle (for `SubprocessTransport` /
   WebSocket-or-Telnet-against-the-simulator)

- **Per-test working directory**, not the repo's shared `native_localfs/`.
  A pytest fixture creates a temp dir, copies (or symlinks) the desired
  config -- e.g. `native_localfs/sim.yaml` -- into
  `<tmpdir>/native_localfs/config.yaml`, and launches `program` with
  `cwd=<tmpdir>`. This sidesteps issue #1 entirely and lets different
  tests boot different configs (e.g. a multi-axis config vs. a
  single-axis one) without fighting over one shared file.
- Build once per session (`pio run -e macos` / `-e linux`, whichever
  matches the host), reuse the same binary across all tests in that run;
  only the working directory (and thus config + WCS/alarm state) is
  per-test.
- Startup is detected by watching stdout for the `Grbl ` banner line
  (same signal `Controller.send_soft_reset()` already waits for), not a
  fixed sleep.
- Teardown: close stdin (or send `Ctrl-X` for a clean stop), then
  terminate/wait with a timeout, same pattern CI already needs for any
  subprocess-based test.

### 4. Directory / API layout

Keep `fixture_tests/` as the home (no renaming yet -- see open question
below):

```
fixture_tests/
  tool/
    transport/
      __init__.py       # Transport protocol
      serial_.py
      websocket_.py
      telnet_.py
      subprocess_.py
    controller.py        # unchanged API, now takes a Transport
    op_entries.py         # unchanged
    utils.py              # unchanged
  fixtures/                # unchanged -- fixture files are transport-agnostic already
  conftest.py              # new: pytest fixtures wiring a --dut CLI option
                            # ("serial:/dev/cu.usbserial-0001",
                            #  "ws://192.168.4.67", "telnet://192.168.4.6",
                            #  "sim:macos", "sim:linux") to a Controller
  run_fixture               # kept for manual one-off use; gains the same
                            # DUT-selection syntax as an alternative to a
                            # bare serial device path
```

Machine-agnostic fixtures (parser/interpreter/flow-control correctness,
things already covered by `LinuxCNCParser.cpp`-style C++ unit tests but
exercised end-to-end through real G-code) run against `sim:` by default in
CI. Machine-specific fixtures (homing, limits, real motion, spindle/VFD
hardware) stay serial/WebSocket/Telnet-only and opt-in (not run in CI,
same as today -- nothing currently runs `fixture_tests` in CI at all).

### 5. CI integration

New job in `.github/workflows/ci.yml`, alongside the existing `tests` job:

```yaml
integration-tests:
  strategy:
    matrix:
      os: [ubuntu-latest, macos-latest]
  runs-on: ${{ matrix.os }}
  steps:
    - uses: actions/checkout@v7
    - name: Set up Python ...
    - name: Install PlatformIO
      run: pip install -r requirements.txt
    - name: Build simulator
      run: pio run -e ${{ matrix.os == 'macos-latest' && 'macos' || 'linux' }}
    - name: Install fixture_tests deps
      run: pip install -r fixture_tests/requirements.txt
    - name: Run machine-agnostic integration tests
      run: pytest fixture_tests -k "not hardware"
```

No Windows here (no `env:windows_x86` native build target used the same
way today, and no real reason to add one just for this).

## Direction check with Bart Dring (2026-09-13, from Mitch)

Pausing here to align on which tests are actually highest-value before
building out more. Context that changes the picture:

- **`posix/simulator_engine.cpp`** (the `engine: simulator` / `sim.N` pins
  used by `native_localfs/sim.yaml`) was originally built as a transport to
  feed motion data to a browser app for animated machine depiction. That
  turned out to be unnecessary -- status reports alone gave the animation
  enough fidelity -- so `simulator_engine.cpp` is now a historical
  artifact. Might be repurposable for testing, but doubtful. **Not** the
  thing to build the "highest-value tests" around.
- **`capture/gpio.cpp`** is the more promising hook instead: it sits under
  `engine: timed` (`timed_engine.cpp`, the *real* stepping engine, not a
  synthetic one), so tests built on it exercise actual production motion
  code rather than a parallel simulated path. Currently a stub, though:
  `gpio_write()`/`gpio_read()` are no-ops, `get_gpios()` returns a
  hardcoded `0`, and `gpio_send_event()`'s dispatch body is `#if 0`'d out
  -- but the event-reporting *shape* is already there (`gpio_set_event`/
  `gpio_clear_event`/`poll_gpios`, with rate-limiting and edge-detection
  structure already written), just not wired to real state. Finishing
  that would let tests observe actual step/dir pulses and limit-pin
  transitions driven by real motion, reported to test scaffolding --
  a materially different (and probably higher-value) class of test than
  anything the Grbl-protocol-level fixture work above can reach, since
  fixtures only ever see *reported* state (status reports), never the
  underlying pin activity that produced it.

Net effect on this plan: the transport/matcher work above (protocol-level,
via serial/WebSocket/Telnet/stdio) and a possible future GPIO-event-level
harness (via `capture/gpio.cpp`) are complementary, not competing --
different layers of the stack, different kinds of bugs each can catch.
Where to invest next is the open question.

## Open questions for you

1. **Naming/scope**: keep everything under `fixture_tests/` (least churn,
   but the name undersells "can also drive a live simulator process"), or
   rename/restructure into something like `integration_tests/` with
   `fixture_tests`'s current content folded in? I lean toward keeping the
   name and just adding to it, but it's your call.
2. **Test runner**: `run_fixture` is currently a standalone script with
   its own pass/fail printing, not pytest. Do you want fixture files to
   become individually discoverable pytest tests (one test function per
   `.fnctest` fixture, via a `conftest.py` collector), or keep `run_fixture` as
   the single entry point and just add DUT-selection to it? Pytest buys
   you `-k` filtering, CI-friendly JUnit XML, parallelism (`pytest-xdist`)
   -- but it's a bigger change to the existing tool than strictly
   necessary.
3. Should I go ahead and prototype the `Transport` abstraction +
   `WebSocketTransport`/`TelnetTransport`/`SubprocessTransport` now (low
   risk, additive, `SerialTransport` stays byte-for-byte the same
   behavior), or hold for your review of this plan first?
