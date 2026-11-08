"""Host tests: independent reversal, feed vector, bounded streaming, faults."""
import importlib.util
from pathlib import Path
import time
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "gamepad_xy", Path(__file__).resolve().parents[1] / "gamepad_motor_controller_xy.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class FakePort:
    def __init__(self):
        self.writes = []
        self.data = bytearray()
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.data)

    def read(self, count):
        result = bytes(self.data[:count])
        del self.data[:count]
        return result

    def write(self, data):
        self.writes.append(data)

    def close(self):
        self.closed = True


def board():
    b = m.Board.__new__(m.Board)
    b.port = FakePort()
    b.buffer = bytearray()
    b.pending = None
    b.state = "Idle"
    b.free = b.capacity = 15
    b.credits = m.MAX_QUEUED_SEGMENTS
    b.active = True
    b.last_status = b.last_query = time.monotonic()
    return b


class MotionTests(unittest.TestCase):
    def setUp(self):
        # Testy używają stałych nastaw, niezależnie od strojenia pada przez użytkownika.
        self.limits = m.MotionLimits(1200.0, 600.0, 40.0, 30.0)
        settings = patch.multiple(m, SEGMENT_SECONDS=0.04, DEADZONE=0.08, EXPO=1.8)
        settings.start()
        self.addCleanup(settings.stop)

    def test_reversing_y_keeps_x_velocity(self):
        motion = m.Motion(self.limits)
        for _ in range(100):
            motion.segment(1, 1)
        previous = motion.vx
        ys = []
        for _ in range(100):
            command = motion.segment(1, -1)
            self.assertAlmostEqual(motion.vx, previous)
            self.assertGreater(float(command.split(" X")[1].split()[0]), 0)
            ys.append(motion.vy)
        self.assertGreater(ys[0], 0)
        self.assertLess(ys[-1], 0)
        self.assertLessEqual(max(abs(b-a) for a, b in zip(ys, ys[1:])),
                             self.limits.y_accel * m.SEGMENT_SECONDS + 1e-9)

    def test_centering_y_does_not_stop_x(self):
        motion = m.Motion(self.limits)
        for _ in range(100):
            motion.segment(1, 1)
        for _ in range(100):
            motion.segment(1, 0)
        self.assertEqual(motion.vy, 0)
        self.assertAlmostEqual(motion.vx, self.limits.x_speed / 60)

    def test_feed_is_vector_norm(self):
        motion = m.Motion(self.limits)
        for _ in range(100):
            command = motion.segment(1, 1)
        feed = float(command.rsplit("F", 1)[1])
        self.assertAlmostEqual(feed, (self.limits.x_speed**2 + self.limits.y_speed**2)**0.5, places=4)

    def test_deadzone_and_inversion(self):
        self.assertEqual(m.stick_value(0.03), 0)
        self.assertEqual(m.stick_value(1), 1)
        self.assertEqual(m.stick_value(1, True), -1)
        self.assertAlmostEqual(m.stick_value(-0.5), -m.stick_value(0.5))


class PadDetectionTests(unittest.TestCase):
    # Mapowania SDL obserwowane / dokumentowane dla każdego scenariusza.
    SDL = {
        ("linux", "PS5"): {"leftx": "a0", "lefty": "a1", "rightx": "a3", "righty": "a4", "b": "b1"},
        ("linux", "XBOX"): {"leftx": "a0", "lefty": "a1", "rightx": "a3", "righty": "a4", "b": "b1"},
        ("windows", "PS5"): {"leftx": "a0", "lefty": "a1", "rightx": "a2", "righty": "a3", "b": "b1"},
        ("windows", "XBOX"): {"leftx": "a0", "lefty": "a1", "rightx": "a3", "righty": "a4", "b": "b1"},
    }
    NAMES = {
        ("linux", "PS5"): "Sony Interactive Entertainment DualSense Wireless Controller",
        ("linux", "XBOX"): "Microsoft X-Box One S pad",
        ("windows", "PS5"): "PS5 Controller",
        ("windows", "XBOX"): "Xbox One Controller",
    }
    EXPECTED = {
        ("linux", "PS5"): (0, 3, 1),
        ("linux", "XBOX"): (0, 3, 1),
        ("windows", "PS5"): (0, 2, 1),
        ("windows", "XBOX"): (0, 3, 1),
    }

    def resolve(self, name, mapping, platform="linux", **overrides):
        values = {"X_STICK_AXIS": None, "Y_STICK_AXIS": None, "STOP_BUTTON": None, **overrides}
        with patch.multiple(m, **values):
            return m.resolve_layout(name, mapping, platform)

    def test_detects_pad_kind_by_name(self):
        for (_, kind), name in self.NAMES.items():
            self.assertEqual(m.detect_pad_kind(name), kind, name)
        self.assertEqual(m.detect_pad_kind("DualSense Wireless Controller"), "PS5")
        self.assertEqual(m.detect_pad_kind("Xbox Wireless Controller"), "XBOX")
        self.assertIsNone(m.detect_pad_kind("Generic USB Joystick"))

    def test_all_scenarios_with_sdl_mapping(self):
        for scenario, expected in self.EXPECTED.items():
            platform, _ = scenario
            kind, layout, source = self.resolve(self.NAMES[scenario], self.SDL[scenario], platform)
            self.assertEqual((kind, layout, source), (scenario[1], expected, "mapowanie SDL"), scenario)

    def test_all_scenarios_without_sdl_mapping(self):
        for scenario, expected in self.EXPECTED.items():
            platform, kind = scenario
            self.assertEqual(self.resolve(self.NAMES[scenario], {}, platform)[1], expected, scenario)

    def test_ps5_linux_keeps_original_layout(self):
        self.assertEqual(self.resolve(self.NAMES[("linux", "PS5")], {}, "linux")[1], (0, 3, 1))

    def test_unknown_pad_uses_original_layout(self):
        for platform in ("linux", "windows"):
            self.assertEqual(self.resolve("Generic USB Joystick", {}, platform)[:2], ("nieznany", (0, 3, 1)))

    def test_manual_indices_override_detection(self):
        scenario = ("windows", "XBOX")
        self.assertEqual(self.resolve(self.NAMES[scenario], self.SDL[scenario], "windows", Y_STICK_AXIS=5)[1],
                         (0, 5, 1))

    def test_windows_disables_rawinput_and_linux_keeps_display(self):
        with patch.dict(m.os.environ, {}, clear=True), patch.object(m, "host_platform", return_value="windows"):
            m.configure_sdl()
            self.assertEqual(m.os.environ.get("SDL_JOYSTICK_RAWINPUT"), "0")
            self.assertEqual(m.os.environ.get("SDL_VIDEODRIVER"), "dummy")
        with patch.dict(m.os.environ, {"DISPLAY": ":0"}, clear=True), \
                patch.object(m, "host_platform", return_value="linux"):
            m.configure_sdl()
            self.assertNotIn("SDL_JOYSTICK_RAWINPUT", m.os.environ)
            self.assertNotIn("SDL_VIDEODRIVER", m.os.environ)
        with patch.dict(m.os.environ, {}, clear=True), patch.object(m, "host_platform", return_value="linux"):
            m.configure_sdl()
            self.assertEqual(m.os.environ.get("SDL_VIDEODRIVER"), "dummy")


class YamlTests(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'config.yaml'
            path.write_text(text)
            return m.load_yaml_limits(path)

    def test_local_yaml_values_drive_full_stick_speed(self):
        template = '''axes:
  x:
    max_rate_mm_per_min: SPEED
    acceleration_mm_per_sec2: 100
  y:
    max_rate_mm_per_min: 250
    acceleration_mm_per_sec2: 50
'''
        for speed in (300, 900):
            limits = self.load(template.replace('SPEED', str(speed)))
            motion = m.Motion(limits)
            for _ in range(100):
                command = motion.segment(1, 0)
            self.assertAlmostEqual(float(command.rsplit('F', 1)[1]), speed, places=2)
            self.assertEqual(limits.y_speed, 250)
            self.assertEqual(limits.y_accel, 50)

    def test_missing_axis_or_field_fails(self):
        with self.assertRaisesRegex(ValueError, 'Brak pola axes/x/max_rate'):
            self.load('axes: {x: {}}')

    def test_invalid_limit_never_gets_a_fallback(self):
        for invalid in (0, -1, float('nan'), float('inf'), True, None, 'bad'):
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                m.read_motion_limits(lambda path: invalid)


class ProtocolTests(unittest.TestCase):
    def test_runtime_limit_readback(self):
        b = board()
        b.command = lambda text: '$/axes/x/max_rate_mm_per_min=1200.000\nok'
        self.assertEqual(b.config_number('axes/x/max_rate_mm_per_min'), 1200)

    def test_missing_limit_fails_clearly(self):
        b = board()
        b.command = lambda text: 'ok'
        with self.assertRaisesRegex(RuntimeError, 'Nie można odczytać limitu'):
            b.config_number('axes/x/max_rate_mm_per_min')

    def test_start_uses_active_board_limits_without_local_defaults(self):
        b = board()
        b.active = False
        def wait_idle(timeout):
            b.free = 15
            b.state = 'Idle'
        b.wait_idle = wait_idle
        b.read_lines = lambda: []
        replies = {'$I': '[VER:FluidNC]', '$Config/Filename': '$Config/Filename=custom.yaml',
                   '$Report/Status': '$Report/Status=3'}
        b.command = lambda text: replies[text]
        values = {'axes/x/max_rate_mm_per_min': 7000,
                  'axes/y/max_rate_mm_per_min': 8000,
                  'axes/x/acceleration_mm_per_sec2': 900,
                  'axes/y/acceleration_mm_per_sec2': 1000}
        b.config_number = lambda path: values[path]
        with patch.object(m.time, 'sleep'):
            b.initialize()
        self.assertEqual(b.limits, m.MotionLimits(7000, 8000, 900, 1000))
        self.assertTrue(b.active)

    def test_ok_is_required_before_next_jog(self):
        b = board()
        b.jog("$J=G91 G21 X0.1 F60")
        self.assertFalse(b.can_send())
        b.consume("ok")
        self.assertTrue(b.can_send())

    def test_queue_full_waits_for_fresh_buffer_report(self):
        b = board()
        for _ in range(m.MAX_QUEUED_SEGMENTS):
            self.assertTrue(b.can_send())
            b.jog("$J=G91 G21 X0.1 F60")
            b.consume("ok")
        self.assertFalse(b.can_send())
        b.consume(f"<Jog|MPos:0,0,0|Bf:{15-m.MAX_QUEUED_SEGMENTS},128>")
        self.assertFalse(b.can_send())
        b.consume(f"<Jog|MPos:0,0,0|Bf:{16-m.MAX_QUEUED_SEGMENTS},128>")
        self.assertTrue(b.can_send())

    def test_faults_stop_streaming(self):
        for line in ("error:8", "ALARM:3", "<Hold:0|MPos:0,0,0>",
                     "Grbl 4.x [FluidNC]", "[MSG:RST]"):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                board().consume(line)

    def test_lost_status_or_ack_times_out(self):
        for field in ("last_status", "pending"):
            b = board()
            setattr(b, field, time.monotonic() - m.REPLY_TIMEOUT - 1)
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                b.service()

    def test_fragmented_serial_lines(self):
        b = board()
        b.port.data.extend(b"<Jog|Bf:12,128>\r")
        self.assertEqual(b.read_lines(), [])
        b.port.data.extend(b"\nok\r\n")
        self.assertEqual(b.read_lines(), ["<Jog|Bf:12,128>", "ok"])

    def test_cancel_uses_realtime_byte_and_waits_for_idle(self):
        b = board()
        states = []
        def service():
            states.append(b.state)
            b.consume("<Idle|MPos:0,0,0|Bf:15,128>")
        b.service = service
        b.cancel()
        self.assertEqual(b.port.writes, [b"\x85"])
        self.assertEqual(states, ["Unknown"])
        self.assertEqual(b.state, "Idle")


if __name__ == "__main__":
    unittest.main()
