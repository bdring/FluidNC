#!/usr/bin/env python3
"""Test silników X/Y bez pada: krótki jog tam i z powrotem na każdej osi.

Pokazuje pozycję FluidNC (MPos) przed i po ruchu. Jeśli MPos się zmienia,
a silnik stoi, problem jest w sprzęcie (zasilanie, sterownik, kabel, enable).
Jeśli MPos się nie zmienia, problem jest po stronie firmware/konfiguracji.
"""
from __future__ import annotations

import argparse
import re
import sys
import time

from gamepad_motor_controller_xy import BAUD_RATE, BOOT_WAIT_SECONDS, SERIAL_PORT, find_serial_port

# ---- USTAWIENIA DO EDYCJI ---------------------------------------------------
TEST_AXES = "XY"                 # które osie testować: "X", "Y" lub "XY"
TEST_DISTANCE_MM = 20.0          # długość ruchu w każdą stronę (max_travel w configu: 100 mm)
TEST_FEED_MM_PER_MIN = 2000.0    # prędkość ruchu (max_rate w configu: 5000 mm/min)
TEST_REPEATS = 3                 # ile razy tam i z powrotem na każdej osi
# ---------------------------------------------------------------------------


class Link:
    def __init__(self, port):
        import serial
        self.port = serial.Serial(port=None, baudrate=BAUD_RATE, timeout=0, write_timeout=0.25)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port
        self.port.open()
        self.buffer = ""

    def lines(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.buffer += self.port.read(self.port.in_waiting).decode("utf-8", errors="replace")
            while "\n" in self.buffer:
                line, self.buffer = self.buffer.split("\n", 1)
                if line.strip():
                    yield line.strip()
            time.sleep(0.01)

    def status(self):
        self.port.write(b"?")
        for line in self.lines(1.0):
            if line.startswith("<"):
                return line
        raise RuntimeError("Brak raportu stanu od FluidNC.")

    def command(self, text):
        self.port.write((text + "\n").encode("ascii"))
        for line in self.lines(2.0):
            if line == "ok":
                return
            if line.startswith(("error:", "ALARM:")):
                raise RuntimeError(f"{text} -> {line}")
        raise RuntimeError(f"Brak odpowiedzi na {text}")

    def wait_idle(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            report = self.status()
            state = report[1:].split("|", 1)[0].split(":", 1)[0]
            if state == "Idle":
                return report
            if state not in ("Jog", "Run"):
                raise RuntimeError(f"Stan {state}: {report}")
            time.sleep(0.1)
        raise RuntimeError("Ruch nie zakończył się w czasie.")


def mpos(report):
    match = re.search(r"MPos:([-\d.]+),([-\d.]+)", report)
    return (float(match[1]), float(match[2])) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=SERIAL_PORT, help="np. COM5 lub /dev/ttyUSB0 (domyślnie autowykrywanie)")
    parser.add_argument("--axes", default=TEST_AXES, help=f"osie do testu, np. X, Y, XY (domyślnie {TEST_AXES})")
    parser.add_argument("--distance", type=float, default=TEST_DISTANCE_MM, help=f"mm w każdą stronę (domyślnie {TEST_DISTANCE_MM:g})")
    parser.add_argument("--feed", type=float, default=TEST_FEED_MM_PER_MIN, help=f"mm/min (domyślnie {TEST_FEED_MM_PER_MIN:g})")
    parser.add_argument("--repeats", type=int, default=TEST_REPEATS, help=f"powtórzenia tam i z powrotem (domyślnie {TEST_REPEATS})")
    args = parser.parse_args()
    axes = [a for a in args.axes.upper() if a in "XY"]
    if not axes or args.distance <= 0 or args.feed <= 0 or args.repeats < 1:
        parser.error("Podaj osie X/Y oraz dodatnie --distance, --feed i --repeats.")

    port = args.port or find_serial_port()
    print(f"Port płytki: {port}; czekam {BOOT_WAIT_SECONDS:g} s na start ESP.")
    link = Link(port)
    try:
        time.sleep(BOOT_WAIT_SECONDS)
        link.port.reset_input_buffer()
        start = link.wait_idle(3.0)
        print(f"Start: {start}")
        for axis in axes:
            index = "XY".index(axis)
            for repeat in range(1, args.repeats + 1):
                before = mpos(link.status())
                for sign in (1, -1):
                    move = f"$J=G91 G21 {axis}{sign * args.distance:g} F{args.feed:g}"
                    print(f"-> [{repeat}/{args.repeats}] {move}")
                    link.command(move)
                    report = link.wait_idle(args.distance / args.feed * 60 + 5)
                    print(f"   {report}")
                    if sign == 1:
                        moved = mpos(report)[index] - before[index]
                        verdict = "OK (firmware wykonał ruch)" if abs(moved - args.distance) < 0.01 else "BRAK RUCHU w firmware"
                        print(f"   {axis}: zmiana MPos {moved:+.3f} mm — {verdict}")
        print("\nJeśli MPos się zmieniało, a silnik stał: sprawdź zasilanie silników, "
              "sterowniki (A4988/TMC) i kable. Jeśli MPos stało: problem w firmware/konfiguracji.")
    except KeyboardInterrupt:
        link.port.write(b"\x85")
        print("\nPrzerwano, wysłano jog cancel.")
        return 1
    except Exception as exc:
        link.port.write(b"\x85")
        print(f"\nBłąd: {exc}")
        return 1
    finally:
        link.port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
