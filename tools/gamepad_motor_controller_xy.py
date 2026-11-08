#!/usr/bin/env python3
"""Pad PS5 / Xbox -> FluidNC XY (Linux i Windows). Konfiguracja poniżej; instrukcja: GAMEPAD-XY.md.

--monitor: tylko odczyt pada, bez otwierania portu płytki.
--dry-run: drukowanie ruchów, bez otwierania portu płytki.
"""
from __future__ import annotations

import argparse
import math
import os
import re
import signal
import time
from pathlib import Path
from typing import NamedTuple

os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
os.environ.setdefault("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1")

class PadLayout(NamedTuple):
    x_axis: int                  # lewy drążek, poziomo
    y_axis: int                  # prawy drążek, poziomo
    stop_button: int             # kółko (PS5) / B (Xbox)


# Surowy układ zależy od systemu i sterownika, więc głównym źródłem jest mapowanie
# SDL. Te układy są zapasowe, gdy SDL nie zna pada:
# - Linux: sterowniki jądra hid-playstation / xpad (LX, LY, L2, RX, RY, R2),
# - Windows: Xbox przez XInput (LX, LY, LT, RX, RY, RT),
#            PS5 przez HIDAPI SDL (LX, LY, RX, RY, L2, R2).
FALLBACK_LAYOUTS = {
    ("linux", "PS5"): PadLayout(x_axis=0, y_axis=3, stop_button=1),
    ("linux", "XBOX"): PadLayout(x_axis=0, y_axis=3, stop_button=1),
    ("windows", "PS5"): PadLayout(x_axis=0, y_axis=2, stop_button=1),
    ("windows", "XBOX"): PadLayout(x_axis=0, y_axis=3, stop_button=1),
}
DEFAULT_LAYOUT = PadLayout(x_axis=0, y_axis=3, stop_button=1)  # pierwotny układ skryptu
# Kolejność ma znaczenie: "Xbox Wireless Controller" nie może trafić do PS5.
PAD_NAME_PATTERNS = {
    "XBOX": ("xbox", "x-box", "xinput", "microsoft"),
    "PS5": ("dualsense", "ps5", "playstation", "sony", "wireless controller"),
}


def host_platform():
    return "windows" if os.name == "nt" else "linux"


# ---- USTAWIENIA DO EDYCJI ---------------------------------------------------
SERIAL_PORT = None               # None = autowykrywanie CH340 (Linux/Windows); np. "COM5"
SERIAL_USB_VID = 0x1A86          # CH340 na MKS TinyBee
BAUD_RATE = 115200
GAMEPAD_INDEX = None             # None = pierwszy rozpoznany pad (PS5/Xbox); albo numer z --monitor
X_STICK_AXIS = None              # None = autowykrycie pada (PS5/Xbox); albo własny indeks z --monitor
Y_STICK_AXIS = None              # None = autowykrycie pada (PS5/Xbox); albo własny indeks z --monitor
INVERT_X_STICK = False
INVERT_Y_STICK = False
# Prędkości i przyspieszenia pochodzą z aktywnej konfiguracji FluidNC.
# Lokalny YAML jest używany tylko w --dry-run, bez połączenia z płytką.
OFFLINE_CONFIG = Path(__file__).resolve().parents[1] / "artifacts/mks-tinybee/config.xy-gamepad.yaml"
DEADZONE = 0.08                  # 0..1, szum drążków
EXPO = 1.8                      # 1 = liniowo; >1 = precyzja przy środku
STOP_BUTTON = None              # None = autowykrycie (kółko PS5 / B Xbox); jog cancel, wymaga powrotu do środka
ENABLE_BUTTON = None            # np. 4 = wymagaj trzymania L1; None = bez
SEGMENT_SECONDS = 0.04           # długość jednego odcinka ruchu
MAX_QUEUED_SEGMENTS = 6          # krótka kolejka; zapas hamowania jest sprawdzany z limitów YAML
POLL_SECONDS = 0.005
STATUS_SECONDS = 0.025
REPLY_TIMEOUT = 1.0
BOOT_WAIT_SECONDS = 8.0         # CH340 może zresetować ESP przy otwarciu USB
VERBOSE_COMMANDS = False
# ---------------------------------------------------------------------------


def validate_config():
    values = (EXPO, SEGMENT_SECONDS, POLL_SECONDS,
              STATUS_SECONDS, REPLY_TIMEOUT, BOOT_WAIT_SECONDS)
    if not all(math.isfinite(v) and v > 0 for v in values):
        raise ValueError("EXPO i czasy muszą być dodatnie i skończone.")
    if not 0 <= DEADZONE < 1 or not 1 <= MAX_QUEUED_SEGMENTS <= 8:
        raise ValueError("DEADZONE: 0..1 (bez 1); MAX_QUEUED_SEGMENTS: 1..8.")


class MotionLimits(NamedTuple):
    x_speed: float  # mm/min
    y_speed: float
    x_accel: float  # mm/s²
    y_accel: float


def read_motion_limits(read_number):
    """Jedna mapa pól dla aktywnego FluidNC i lokalnego YAML-a."""
    values = []
    for field in ("max_rate_mm_per_min", "acceleration_mm_per_sec2"):
        for axis in ("x", "y"):
            path = f"axes/{axis}/{field}"
            raw = read_number(path)
            try:
                value = float(raw)
            except (TypeError, ValueError) as exc:
                raise ValueError(f"Nieprawidłowe {path}: {raw!r}") from exc
            if isinstance(raw, bool) or not math.isfinite(value) or value <= 0:
                raise ValueError(f"Nieprawidłowe {path}: {raw!r}")
            values.append(value)
    return MotionLimits(*values)


def load_yaml_limits(filename):
    import yaml
    with Path(filename).open() as source:
        config = yaml.safe_load(source)

    def read_number(path):
        value = config
        try:
            for part in path.split("/"):
                value = value[part]
        except (KeyError, TypeError) as exc:
            raise ValueError(f"Brak pola {path} w {filename}") from exc
        return value

    return read_motion_limits(read_number)


def show_motion_limits(limits, source):
    print(f"Źródło prędkości i przyspieszeń: {source}")
    for axis, speed, accel in (("X", limits.x_speed, limits.x_accel),
                               ("Y", limits.y_speed, limits.y_accel)):
        print(f"{axis}: maksimum {speed:g} mm/min ({speed / 60:g} mm/s), "
              f"przyspieszenie {accel:g} mm/s²")
        horizon = MAX_QUEUED_SEGMENTS * SEGMENT_SECONDS
        if speed / 60 > 2 * accel * horizon:
            print(f"Uwaga: krótka kolejka może ograniczyć prędkość osi {axis}.")


def stick_value(raw, invert=False):
    raw = max(-1.0, min(1.0, raw)) * (-1 if invert else 1)
    if abs(raw) <= DEADZONE:
        return 0.0
    return math.copysign(((abs(raw) - DEADZONE) / (1 - DEADZONE)) ** EXPO, raw)


def approach(current, target, delta):
    return current + max(-delta, min(delta, target - current))


class Motion:
    """Osobne rampy prędkości. Zmiana Y nie zeruje prędkości X."""
    def __init__(self, limits):
        self.limits = limits
        self.reset()

    def reset(self):
        self.vx = self.vy = 0.0
        self.rx = self.ry = 0.0

    def segment(self, x, y):
        self.vx = approach(self.vx, x * self.limits.x_speed / 60,
                           self.limits.x_accel * SEGMENT_SECONDS)
        self.vy = approach(self.vy, y * self.limits.y_speed / 60,
                           self.limits.y_accel * SEGMENT_SECONDS)
        # Pozostałości zaokrąglenia nie giną przy małych wychyleniach.
        dx = self.vx * SEGMENT_SECONDS + self.rx
        dy = self.vy * SEGMENT_SECONDS + self.ry
        qx, qy = round(dx, 5), round(dy, 5)
        self.rx, self.ry = dx - qx, dy - qy
        if qx == 0 and qy == 0:
            return None
        # F to prędkość po wektorze XY, a nie limit dzielony między silniki.
        feed = math.hypot(qx, qy) / SEGMENT_SECONDS * 60
        return f"$J=G91 G21 X{qx:.5f} Y{qy:.5f} F{feed:.5f}"


class Board:
    def __init__(self, port, baud):
        import serial
        self.port = serial.Serial(port=None, baudrate=baud, timeout=0,
                                  write_timeout=0.25, exclusive=True)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port
        self.buffer = bytearray()
        self.pending = None
        self.state = "Unknown"
        self.free = None
        self.capacity = None
        self.credits = 0
        self.last_status = self.last_query = 0.0
        self.active = False
        self.original_mask = None
        self.mask_changed = False
        self.limits = None
        self.port.open()

    def write(self, data):
        self.port.write(data)

    def read_lines(self):
        self.buffer.extend(self.port.read(self.port.in_waiting))
        lines = []
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            lines.append(line.decode("utf-8", errors="replace").strip())
        return lines

    def consume(self, line):
        now = time.monotonic()
        if line.startswith("<"):
            self.state = line[1:].split("|", 1)[0].split(":", 1)[0]
            self.last_status = now
            match = re.search(r"\|Bf:(\d+),", line)
            if match:
                self.free = int(match[1])
                if self.capacity is not None:
                    self.credits = max(0, MAX_QUEUED_SEGMENTS - (self.capacity - self.free))
            if self.active and self.state not in ("Idle", "Jog"):
                raise RuntimeError(f"Sterownik zgłosił stan {self.state}; zatrzymuję obsługę.")
        elif line == "ok":
            self.pending = None
        elif line.startswith(("error:", "ALARM:")):
            raise RuntimeError(f"FluidNC: {line}")
        elif self.active and (line.startswith("Grbl ") or "[MSG:RST" in line):
            raise RuntimeError("Restart FluidNC podczas sterowania.")

    def service(self):
        for line in self.read_lines():
            self.consume(line)
        now = time.monotonic()
        if now - self.last_query >= STATUS_SECONDS:
            self.write(b"?")
            self.last_query = now
        if self.pending is not None and now - self.pending > REPLY_TIMEOUT:
            raise RuntimeError("Brak potwierdzenia komendy od FluidNC.")
        if self.active and now - self.last_status > REPLY_TIMEOUT:
            raise RuntimeError("Utracono odczyt stanu FluidNC.")

    def command(self, text):
        """Tylko inicjalizacja/porządkowanie, bez ruchów; czekaj na ok."""
        if self.pending is not None:
            raise RuntimeError("Poprzednia komenda nadal czeka na odpowiedź.")
        self.write((text + "\n").encode("ascii"))
        self.pending = time.monotonic()
        replies = []
        while self.pending is not None:
            for line in self.read_lines():
                replies.append(line)
                self.consume(line)
            if self.pending is not None and time.monotonic() - self.pending > REPLY_TIMEOUT:
                raise RuntimeError(f"Brak odpowiedzi na {text}")
            time.sleep(POLL_SECONDS)
        return "\n".join(replies)

    def wait_idle(self, timeout):
        deadline = time.monotonic() + timeout
        # Wymagaj świeżego raportu po anulowaniu, nie starego Idle.
        self.state = "Unknown"
        while time.monotonic() < deadline:
            self.service()
            if self.state == "Idle" and self.pending is None:
                return
            if self.state not in ("Unknown", "Idle", "Jog"):
                raise RuntimeError(f"Wymagany Idle, jest {self.state}. Nie odblokowuję alarmów automatycznie.")
            time.sleep(POLL_SECONDS)
        raise RuntimeError("Nie uzyskano Idle po zatrzymaniu jog.")

    def initialize(self):
        print("Otwieranie USB może zrestartować ESP; czekam na uruchomienie.")
        time.sleep(BOOT_WAIT_SECONDS)
        self.read_lines()  # wyłącznie komunikaty startowe
        self.wait_idle(3.0)
        info = self.command("$I")
        if "FluidNC" not in info:
            raise RuntimeError("Podłączony sterownik nie potwierdził FluidNC.")
        filename = self.command("$Config/Filename")
        self.limits = read_motion_limits(self.config_number)
        show_motion_limits(self.limits, f"aktywna konfiguracja płytki ({filename.splitlines()[0]})")
        result = self.command("$Report/Status")
        match = re.search(r"\$(?:Report/Status|10)=(\d+)", result, re.I)
        if not match:
            raise RuntimeError(f"Nie można odczytać maski raportów: {result}")
        self.original_mask = int(match[1])
        if self.original_mask != 3:
            self.mask_changed = True
            self.command("$Report/Status=3")
        self.free = None
        self.wait_idle(3.0)
        if self.free is None or self.free < MAX_QUEUED_SEGMENTS:
            raise RuntimeError("Brak informacji o wolnym buforze planera (Bf).")
        self.capacity = self.free
        self.credits = MAX_QUEUED_SEGMENTS
        self.active = True
        print(f"FluidNC: Idle, pojemność planera {self.capacity}. Gotowe.")

    def config_number(self, path):
        reply = self.command(f"$/{path}")
        match = re.search(r"\$/" + re.escape(path) + r"=([-+0-9.eE]+)", reply, re.I)
        if not match:
            raise RuntimeError(f"Nie można odczytać limitu {path}: {reply}")
        value = float(match[1])
        if not math.isfinite(value) or value <= 0:
            raise RuntimeError(f"Nieprawidłowy limit {path}: {value}")
        return value

    def can_send(self):
        return (self.pending is None and self.credits > 0
                and self.state in ("Idle", "Jog"))

    def jog(self, command):
        self.write((command + "\n").encode("ascii"))
        self.pending = time.monotonic()
        self.credits -= 1
        if VERBOSE_COMMANDS:
            print(command)

    def cancel(self):
        self.write(b"\x85")  # jog cancel, bez resetu / feed hold
        self.wait_idle(3.0)
        self.credits = MAX_QUEUED_SEGMENTS

    def close(self):
        try:
            self.active = False
            self.cancel()
            if self.mask_changed:
                self.command(f"$Report/Status={self.original_mask}")
        finally:
            self.port.close()


def detect_pad_kind(name):
    lowered = name.lower()
    for kind, patterns in PAD_NAME_PATTERNS.items():
        if any(pattern in lowered for pattern in patterns):
            return kind
    return None


def sdl_mapping(pygame, index):
    """Mapowanie SDL GameController: fizyczne drążki -> surowe indeksy na tym systemie."""
    try:
        from pygame._sdl2 import controller
        controller.init()
        if controller.is_controller(index):
            return controller.Controller(index).get_mapping()
    except (ImportError, pygame.error):
        pass
    return {}


def mapped_index(mapping, name, prefix):
    match = re.fullmatch(prefix + r"(\d+)", mapping.get(name, ""))
    return int(match[1]) if match else None


def resolve_layout(name, mapping, platform=None):
    """Zwraca (typ pada, układ, źródło). Ręczne indeksy mają pierwszeństwo."""
    platform = platform or host_platform()
    kind = detect_pad_kind(name)
    sdl = PadLayout(mapped_index(mapping, "leftx", "a"),
                    mapped_index(mapping, "rightx", "a"),
                    mapped_index(mapping, "b", "b"))
    if None not in sdl:
        base, source = sdl, "mapowanie SDL"
    elif kind:
        base, source = FALLBACK_LAYOUTS[(platform, kind)], f"zapasowy układ {kind}/{platform}"
    else:
        base, source = DEFAULT_LAYOUT, "domyślny układ (nieznany pad)"
    manual = (X_STICK_AXIS, Y_STICK_AXIS, STOP_BUTTON)
    layout = PadLayout(*(m if m is not None else b for m, b in zip(manual, base)))
    if any(m is not None for m in manual):
        source += " + ręczne ustawienia"
    return kind or "nieznany", layout, source


def configure_sdl():
    """Ustawienia SDL zależne od systemu; muszą być przed pygame.display.init()."""
    if host_platform() == "windows":
        # Z RawInput pad pod sterownikiem "dummy" zwraca same zera.
        # Bez niego Xbox idzie przez XInput/WGI, PS5 przez HIDAPI; oba działają bez okna.
        os.environ.setdefault("SDL_JOYSTICK_RAWINPUT", "0")
        os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    elif not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        os.environ.setdefault("SDL_VIDEODRIVER", "dummy")


def choose_pad_index(pygame):
    """GAMEPAD_INDEX albo pierwszy rozpoznany pad (pomija np. touchpad/czujniki ruchu)."""
    count = pygame.joystick.get_count()
    if GAMEPAD_INDEX is not None:
        if GAMEPAD_INDEX >= count:
            raise RuntimeError(f"Brak pada o indeksie {GAMEPAD_INDEX} (wykryto {count}).")
        return GAMEPAD_INDEX
    if count == 0:
        raise RuntimeError("Nie wykryto pada. Podłącz pada (PS5/Xbox) i spróbuj ponownie.")
    for index in range(count):
        if sdl_mapping(pygame, index) or detect_pad_kind(pygame.joystick.Joystick(index).get_name()):
            return index
    return 0


def get_pad(pygame):
    global X_STICK_AXIS, Y_STICK_AXIS, STOP_BUTTON
    configure_sdl()
    pygame.display.init()  # kolejka zdarzeń SDL wymaga podsystemu video
    pygame.joystick.init()
    index = choose_pad_index(pygame)
    pad = pygame.joystick.Joystick(index)
    kind, layout, source = resolve_layout(pad.get_name(), sdl_mapping(pygame, index))
    X_STICK_AXIS, Y_STICK_AXIS, STOP_BUTTON = layout
    if max(X_STICK_AXIS, Y_STICK_AXIS) >= pad.get_numaxes() or min(X_STICK_AXIS, Y_STICK_AXIS) < 0:
        raise ValueError("Indeks drążka poza zakresem; użyj --monitor.")
    for button in (STOP_BUTTON, ENABLE_BUTTON):
        if button is not None and not 0 <= button < pad.get_numbuttons():
            raise ValueError("Indeks przycisku poza zakresem.")
    print(f"Pad [{index}]: {pad.get_name()}, osi: {pad.get_numaxes()}, przycisków: {pad.get_numbuttons()}")
    print(f"Typ pada: {kind}; X = oś {X_STICK_AXIS}, Y = oś {Y_STICK_AXIS}, stop = przycisk {STOP_BUTTON} ({source})")
    return pad


def inputs(pygame, pad):
    for event in pygame.event.get():
        if event.type == pygame.JOYDEVICEREMOVED and event.instance_id == pad.get_instance_id():
            raise RuntimeError("Odłączono pada.")
        if event.type == pygame.QUIT:
            raise KeyboardInterrupt
    if not pad.get_init():
        raise RuntimeError("Utracono połączenie z padem.")
    return (stick_value(pad.get_axis(X_STICK_AXIS), INVERT_X_STICK),
            stick_value(pad.get_axis(Y_STICK_AXIS), INVERT_Y_STICK))


def run(pygame, pad, board, duration, limits):
    motion = Motion(limits)
    armed = moving = False
    next_segment = 0.0
    started = time.monotonic()
    print("Wycentruj oba drążki, aby uzbroić. Lewy = X, prawy = Y. Kółko / Ctrl+C = stop.")
    while duration is None or time.monotonic() - started < duration:
        x, y = inputs(pygame, pad)
        stopped = pad.get_button(STOP_BUTTON) if STOP_BUTTON is not None else False
        enabled = ENABLE_BUTTON is None or pad.get_button(ENABLE_BUTTON)
        if board:
            board.service()
        if stopped or not enabled or (x == 0 and y == 0):
            if moving:
                if board:
                    board.cancel()
                else:
                    print("STOP (jog cancel)")
                motion.reset()
                moving = False
            if stopped or not enabled:
                armed = False
            elif not armed:
                armed = True
                print("Uzbrojony — drążki sterują silnikami.")
        elif armed:
            now = time.monotonic()
            # Przy sprzęcie o tempie decyduje Bf, a nie szacunek czasu jazdy.
            if (board and board.can_send()) or (board is None and now >= next_segment):
                command = motion.segment(x, y)
                next_segment = now + SEGMENT_SECONDS
                if command:
                    moving = True
                    if board:
                        board.jog(command)
                    else:
                        print(command)
                elif board:
                    # Nie zalewaj planera odcinkami o zerowej długości.
                    time.sleep(SEGMENT_SECONDS)
        time.sleep(POLL_SECONDS)


def find_serial_port():
    # Linux: stabilna nazwa by-id nie zmienia się przy przełączaniu USB (ttyUSB0/1).
    by_id = sorted(Path("/dev/serial/by-id").glob(f"usb-{SERIAL_USB_VID:04x}_*"))
    if len(by_id) == 1:
        return str(by_id[0])
    from serial.tools import list_ports
    ports = [p for p in list_ports.comports() if p.vid == SERIAL_USB_VID]
    if len(ports) == 1:
        return ports[0].device
    available = ", ".join(f"{p.device} ({p.description})" for p in list_ports.comports()) or "brak"
    if not ports:
        raise RuntimeError(f"nie znaleziono płytki (USB VID {SERIAL_USB_VID:04x}); dostępne porty: {available}; użyj --port")
    raise RuntimeError(f"kilka pasujących portów: {available}; wskaż jeden przez --port")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=SERIAL_PORT, help="port płytki, np. COM5 lub /dev/ttyUSB0 (domyślnie autowykrywanie)")
    parser.add_argument("--monitor", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--config", type=Path, help="lokalny YAML, tylko z --dry-run")
    parser.add_argument("--duration", type=float, help="zakończ po N sekundach (opcjonalne)")
    args = parser.parse_args()
    if args.config is not None and not args.dry_run:
        parser.error("--config służy tylko do --dry-run; przy ruchu źródłem jest aktywny YAML płytki")
    validate_config()
    if args.duration is not None and (not math.isfinite(args.duration) or args.duration <= 0):
        parser.error("--duration musi być dodatnie i skończone")
    try:
        import pygame
    except ImportError:
        parser.exit(1, "Brak zależności: pip install -r tools/requirements-gamepad.txt (instrukcja: tools/GAMEPAD-XY.md)\n")
    signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    board = None
    result = 0
    try:
        pad = get_pad(pygame)
        if args.monitor:
            started = time.monotonic()
            while args.duration is None or time.monotonic() - started < args.duration:
                inputs(pygame, pad)
                axes = " ".join(f"{i}:{pad.get_axis(i):+.3f}" for i in range(pad.get_numaxes()))
                buttons = [i for i in range(pad.get_numbuttons()) if pad.get_button(i)]
                print(f"\rOsie: {axes} | Przyciski: {buttons}       ", end="", flush=True)
                time.sleep(0.1)
            print()
        else:
            if args.dry_run:
                filename = args.config or OFFLINE_CONFIG
                limits = load_yaml_limits(filename)
                show_motion_limits(limits, filename)
            else:
                port = args.port or find_serial_port()
                print(f"Port płytki: {port}")
                board = Board(port, BAUD_RATE)
                board.initialize()
                limits = board.limits
            run(pygame, pad, board, args.duration, limits)
    except KeyboardInterrupt:
        print("\nZatrzymuję.")
    except Exception as exc:
        print(f"\nBłąd: {exc}")
        result = 1
    finally:
        if board:
            try:
                board.close()
            except Exception as exc:
                print(f"Nie potwierdzono zatrzymania/przywrócenia raportów: {exc}")
                result = 1
        pygame.quit()
    return result


if __name__ == "__main__":
    raise SystemExit(main())
