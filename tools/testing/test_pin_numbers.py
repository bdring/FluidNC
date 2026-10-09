#!/usr/bin/env python3
"""Pin-number regressions against a complete POSIX firmware build and its schema.

Build with `pio run -e macos` (or `-e linux`), then run:
  python3 tools/testing/test_pin_numbers.py .pio/build/macos/program
The firmware uses temporary native filesystems; no controller is required.
"""
import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from config_schema_adapter import _pin_any


def startup(program, pin):
    with tempfile.TemporaryDirectory(prefix="fluidnc-pin-") as directory:
        root = Path(directory)
        (root / "native_localfs").mkdir()
        (root / "native_sd").mkdir()
        (root / "native_localfs/config.yaml").write_text(
            f"name: Pin regression\nboard: None\nuser_outputs:\n  digital0_pin: '{pin}'\n"
        )
        # Keep the simulator alive until the queued command finishes. Its
        # background tasks need not be shut down to test configuration loading.
        process = subprocess.Popen(
            [str(program), "-c", "$Config/Dump", "-"], cwd=root,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        output = b""
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    if not selector.select(timeout=0.1):
                        continue
                    chunk = os.read(process.stdout.fileno(), 65536)
                    if not chunk:
                        raise AssertionError(f"Simulator exited early: {output.decode(errors='replace')}")
                    output += chunk
                    if re.search(rb"(?:^|\n)ok\r?\n", output):
                        return output.decode(errors="replace")
                raise AssertionError(f"Simulator timed out: {output.decode(errors='replace')}")
        finally:
            process.kill()
            process.wait(timeout=5)
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    args = parser.parse_args()
    program = args.program.resolve()
    invalid = ["gpio.128", "gpio.255", "gpio.256", "gpio.4294967296", "gpio.4294967300",
               "gpio.18446744073709551616", "gpio.4x", "gpio.-1", "gpio.+4", "gpio"]
    for pin in invalid:
        output = startup(program, pin)
        assert f"Setting up pin: {pin} failed:Invalid pin number" in output, output
        assert not re.search(r"User Digital Output: 0 on Pin:gpio\.", output), output
    for pin, expected in [("gpio.4", "gpio.4"), ("GPIO.004:low", "gpio.4:low"),
                          ("void", "NO_PIN")]:
        output = startup(program, pin)
        assert f"User Digital Output: 0 on Pin:{expected}" in output, output
        assert "Invalid pin number" not in output, output
    output = startup(program, "NO_PIN")
    assert "Setting up pin:" not in output and "User Digital Output: 0" not in output, output

    pattern = re.compile(_pin_any({"uart_channel": {"pattern": r"uart_channel[0-9]\.[0-9]+"}})["pattern"])
    for pin in invalid + ["uart_channel0.256"]:
        assert pattern.fullmatch(pin) is None, pin
    for pin in ["gpio.0", "gpio.127", "GPIO.004:low", "uart_channel0.127", "NO_PIN", "void"]:
        assert pattern.fullmatch(pin), pin
    print("PASS: 14 firmware configuration cases and 17 schema cases")


if __name__ == "__main__":
    main()
