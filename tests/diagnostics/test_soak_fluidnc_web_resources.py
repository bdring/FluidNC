import base64
import hashlib
import importlib.util
import json
import socket
import threading
import time
from argparse import Namespace
from pathlib import Path
from types import SimpleNamespace

import pytest


TOOL = Path(__file__).parents[2] / "tools" / "soak_fluidnc_web_resources.py"
HARDENING_ID = "v4.0.4-webhardening-20260820-r2"


def load_tool():
    spec = importlib.util.spec_from_file_location("soak_fluidnc_web_resources", TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def stats_payload(**overrides):
    values = {
        "Diagnostic hardening ID": HARDENING_ID,
        "Diagnostic boot sequence": "7",
        "Diagnostic uptime ms": "100000",
        "Diagnostic reset reason": "3",
        "Free memory": "48.00 KB",
        "Largest free block": "32.00 KB",
        "Heap minimum free": "20.00 KB",
        "Web WS pending": "0",
        "Web WS active": "0",
        "Web WS connecting": "0",
        "Web WS deferred closes": "0",
        "Web file streams": "0",
        "Web heavy HTTP active": "1",
        "Web WS limit rejections": "0",
        "Web WS heap rejections": "0",
        "Web WS recovery admissions": "0",
        "Web WS zero idle ms": "70000",
        "Web file starts": "0",
        "Web file completions": "0",
        "Web file rejections": "0",
        "Web heavy HTTP rejections": "0",
        "Web requests created": "100",
        "Web requests destroyed": "98",
        "Web request owners allocated": "100",
        "Web request owners deallocated": "98",
        "Async TCP clients created": "50",
        "Async TCP clients destroyed": "47",
        "Async TCP events created": "400",
        "Async TCP events destroyed": "399",
        "Async TCP event queue depth": "1",
        "Async TCP event queue high water": "4",
        "Async TCP RX timeouts": "0",
        "Async TCP accept event allocation failures": "0",
        "Async TCP early RST accept cleanups": "0",
        "Async TCP accept admission rejections": "0",
        "Async TCP server pending accepts": "0",
        "Async TCP accept last observed free": "48.00 KB",
        "Async TCP accept last largest block": "32.00 KB",
        "Async TCP accept last effective free": "41.00 KB",
        "Async TCP accept last live clients": "1",
        "Async TCP accept callbacks": "5",
        "Async TCP accept null PCBs": "0",
        "Async TCP accept last null PCB error": "0",
        "Async TCP accept client allocation failures": "0",
        "Async TCP accept client setup failures": "0",
        "Async WebSocket reject abort calls": "0",
        "Async WebSocket reject abort max us": "0",
        "TCP PCBs active": "3",
        "TCP PCBs time wait": "0",
        "TCP accept PCB active+TIME_WAIT peak": "3",
        "TCP listener backlog": "5",
        "TCP listener accepts pending": "0",
    }
    values.update({key: str(value) for key, value in overrides.items()})
    return json.dumps(
        {"cmd": "420", "status": "ok", "data": [{"id": key, "value": value} for key, value in values.items()]}
    ).encode()


def test_parse_stats_normalizes_byte_units_and_counters():
    tool = load_tool()

    parsed = tool.parse_esp420(stats_payload(**{"Free memory": "1.50 KB", "Largest free block": "2048 B"}))

    assert parsed["Free memory"] == 1536
    assert parsed["Largest free block"] == 2048
    assert parsed["Web requests created"] == 100
    assert parsed["Diagnostic hardening ID"] == HARDENING_ID


def test_resource_check_accounts_for_the_in_band_esp420_heavy_owner():
    tool = load_tool()

    observed = tool.parse_esp420(stats_payload())
    assert tool._resource_errors(observed) == []

    missing_self = dict(observed, **{"Web heavy HTTP active": 0})
    duplicate_owner = dict(observed, **{"Web heavy HTTP active": 2})
    assert any("expected in-band ESP420 owner 1" in error for error in tool._resource_errors(missing_self))
    assert any("expected in-band ESP420 owner 1" in error for error in tool._resource_errors(duplicate_owner))


def test_static_disconnect_attribution_accepts_tcp_or_file_rejections_and_fails_shortfall():
    tool = load_tool()

    tool.validate_static_disconnect_attribution(5, file_rejections=2, accept_rejections=3)
    tool.validate_static_disconnect_attribution(5, file_rejections=5, accept_rejections=0)

    with pytest.raises(tool.SoakAbort, match="2 file-stream and 2 TCP-admission"):
        tool.validate_static_disconnect_attribution(5, file_rejections=2, accept_rejections=2)


def test_preflight_fails_closed_on_wrong_build_non_idle_or_existing_resources():
    tool = load_tool()
    snapshot = tool.Snapshot.for_test(
        state="Alarm",
        stats=tool.parse_esp420(
            stats_payload(
                **{
                    "Diagnostic hardening ID": "wrong",
                    "Web WS active": 1,
                    "Web file streams": 1,
                }
            )
        ),
    )

    errors = tool.validate_preflight(snapshot, HARDENING_ID)

    assert any("Idle" in error for error in errors)
    assert any("hardening ID" in error for error in errors)
    assert any("Web WS active" in error for error in errors)
    assert any("Web file streams" in error for error in errors)


def test_preflight_uses_the_in_band_observer_floor_not_the_raw_websocket_floor():
    tool = load_tool()
    snapshot = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Free memory": "30.00 KB", "Largest free block": "24.00 KB"}))
    )

    errors = tool.validate_preflight(snapshot, HARDENING_ID)

    assert not any("free heap" in error for error in errors)

    snapshot.stats["Free memory"] = 27 * 1024
    errors = tool.validate_preflight(snapshot, HARDENING_ID)

    assert any("in-band ESP420 observer floor" in error for error in errors)


def test_ramp_releases_the_heavy_observer_before_first_websocket_admission():
    source = (Path(__file__).parents[2] / "tools" / "soak_fluidnc_web_resources.py").read_text(encoding="utf-8")
    body = source[source.index("    def _stage_ramp") : source.index("    def _stage_churn")]

    snapshot = body.index('self._capture_guard("ramp-before")')
    grace = body.index("time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)")
    handshake = body.index("RawWebSocket.connect")

    assert snapshot < grace < handshake


def test_postflight_detects_reboot_identity_drift_and_unrecovered_lifecycle_gaps():
    tool = load_tool()
    before = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    after_values = tool.parse_esp420(
        stats_payload(
            **{
                "Diagnostic boot sequence": 8,
                "Diagnostic uptime ms": 10,
                "Web WS active": 1,
                "Web requests created": 140,
                "Web requests destroyed": 120,
            }
        )
    )
    after = tool.Snapshot.for_test(stats=after_values, config_sha256="changed")

    errors = tool.validate_postflight(before, after)

    assert any("boot sequence" in error for error in errors)
    assert any("uptime" in error for error in errors)
    assert any("config" in error.lower() for error in errors)
    assert any("Web WS active" in error for error in errors)
    assert any("request lifecycle gap" in error for error in errors)


def test_postflight_rejects_even_one_new_permanent_object_above_baseline_gap():
    tool = load_tool()
    before = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    after = tool.Snapshot.for_test(
        stats=tool.parse_esp420(
            stats_payload(**{"Web requests created": 120, "Web requests destroyed": 117})
        )
    )

    errors = tool.validate_postflight(before, after)

    assert any("request lifecycle gap" in error for error in errors)


class HandshakeServer:
    def __init__(self, accept=True, send_ping=False, send_close=False, trickle_delay=0.0):
        self.accept = accept
        self.send_ping = send_ping
        self.send_close = send_close
        self.trickle_delay = trickle_delay
        self.pong_seen = threading.Event()
        self.close_reply_seen = threading.Event()
        self.ready = threading.Event()
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.port = self.listener.getsockname()[1]
        self.thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self):
        self.thread.start()
        self.ready.wait(2)
        return self

    def __exit__(self, *_):
        self.listener.close()
        self.thread.join(2)

    def _serve(self):
        self.ready.set()
        connection, _ = self.listener.accept()
        with connection:
            request = b""
            while b"\r\n\r\n" not in request:
                request += connection.recv(4096)
            if not self.accept:
                connection.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")
                return
            headers = {}
            for line in request.decode("ascii").split("\r\n")[1:]:
                if ":" in line:
                    key, value = line.split(":", 1)
                    headers[key.lower()] = value.strip()
            accept = base64.b64encode(
                hashlib.sha1((headers["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
            )
            response = (
                b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                + b"Sec-WebSocket-Accept: "
                + accept
                + b"\r\n\r\n"
            )
            if self.trickle_delay:
                for byte in response:
                    try:
                        connection.sendall(bytes((byte,)))
                    except OSError:
                        return
                    time.sleep(self.trickle_delay)
            else:
                connection.sendall(response)
            if self.send_ping:
                connection.sendall(b"\x89\x03FNC")
                connection.settimeout(2)
                frame = connection.recv(64)
                if frame and frame[0] & 0x0F == 0x0A:
                    self.pong_seen.set()
            elif self.send_close:
                connection.sendall(b"\x88\x02\x03\xe8")
                connection.settimeout(2)
                frame = connection.recv(64)
                if frame and frame[0] & 0x0F == 0x08:
                    self.close_reply_seen.set()
            else:
                connection.settimeout(2)
                try:
                    connection.recv(64)
                except socket.timeout:
                    pass


def test_raw_websocket_accepts_exact_handshake_and_auto_pongs_without_app_data():
    tool = load_tool()
    with HandshakeServer(send_ping=True) as server:
        client = tool.RawWebSocket.connect("127.0.0.1", server.port, timeout=2, auto_pong=True)
        try:
            assert client.accepted
            assert client.http_status == 101
            assert server.pong_seen.wait(2)
            assert client.ping_frames_received == 1
            assert client.last_ping_payload == b"FNC"
            assert client.application_frames_sent == 0
        finally:
            client.close(graceful=False)


def test_raw_websocket_classifies_policy_rejection_without_retrying():
    tool = load_tool()
    with HandshakeServer(accept=False) as server:
        client = tool.RawWebSocket.connect("127.0.0.1", server.port, timeout=2, auto_pong=False)

    assert not client.accepted
    assert client.http_status == 503
    assert client.application_frames_sent == 0


def test_raw_websocket_labels_peer_eof_before_http_status_for_failure_attribution():
    tool = load_tool()
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)

    def close_after_request():
        connection, _address = listener.accept()
        with connection:
            connection.recv(4096)

    thread = threading.Thread(target=close_after_request, daemon=True)
    thread.start()
    try:
        client = tool.RawWebSocket.connect("127.0.0.1", listener.getsockname()[1], timeout=2, auto_pong=False)
    finally:
        listener.close()
        thread.join(2)

    assert not client.accepted
    assert client.http_status == 0
    assert client.failure_kind == "eof_before_http_status"
    assert client.handshake_response_bytes == 0
    assert client.handshake_elapsed_ms >= 0


def test_raw_websocket_labels_connection_reset_as_peer_transport_abort(monkeypatch):
    tool = load_tool()

    def reset(*_args, **_kwargs):
        raise ConnectionResetError("peer reset")

    monkeypatch.setattr(tool.socket, "create_connection", reset)

    client = tool.RawWebSocket.connect("127.0.0.1", 80, timeout=2, auto_pong=False)

    assert not client.accepted
    assert client.http_status == 0
    assert client.failure_kind == "peer_transport_abort"
    assert client.handshake_response_bytes == 0


def test_raw_websocket_replies_to_server_close_with_protocol_close_only():
    tool = load_tool()
    with HandshakeServer(send_close=True) as server:
        client = tool.RawWebSocket.connect("127.0.0.1", server.port, timeout=2, auto_pong=True)
        try:
            assert client.accepted
            assert client.server_closed.wait(2)
            assert server.close_reply_seen.wait(2)
            assert client.application_frames_sent == 0
        finally:
            client.close(graceful=False)


def test_websocket_handshake_has_hard_total_deadline_even_when_headers_trickle():
    tool = load_tool()
    with HandshakeServer(trickle_delay=0.05) as server:
        started = time.monotonic()
        client = tool.RawWebSocket.connect("127.0.0.1", server.port, timeout=0.15, auto_pong=False)
        elapsed = time.monotonic() - started

    assert not client.accepted
    assert elapsed < 0.5


def test_read_only_http_has_hard_header_deadline_even_when_bytes_trickle():
    tool = load_tool()

    class TrickleHeaderServer:
        def __init__(self):
            self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.listener.bind(("127.0.0.1", 0))
            self.listener.listen()
            self.port = self.listener.getsockname()[1]
            self.thread = threading.Thread(target=self.serve, daemon=True)

        def __enter__(self):
            self.thread.start()
            return self

        def __exit__(self, *_args):
            self.listener.close()
            self.thread.join(2)

        def serve(self):
            connection, _ = self.listener.accept()
            with connection:
                request = b""
                while b"\r\n\r\n" not in request:
                    request += connection.recv(4096)
                for byte in b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n":
                    try:
                        connection.sendall(bytes((byte,)))
                    except OSError:
                        return
                    time.sleep(0.05)

    with TrickleHeaderServer() as server:
        client = tool.FluidNCReadOnlyClient("127.0.0.1", server.port, timeout=0.15)
        started = time.monotonic()
        with pytest.raises(tool.SoakAbort, match="deadline"):
            client.get("/", allowed_statuses=(101,))
        elapsed = time.monotonic() - started

    assert elapsed < 0.5


def test_raw_http_parser_decodes_chunked_command_response_with_total_limit():
    tool = load_tool()

    class ChunkedServer:
        def __init__(self):
            self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.listener.bind(("127.0.0.1", 0))
            self.listener.listen()
            self.port = self.listener.getsockname()[1]
            self.thread = threading.Thread(target=self.serve, daemon=True)

        def serve(self):
            connection, _ = self.listener.accept()
            with connection:
                request = b""
                while b"\r\n\r\n" not in request:
                    request += connection.recv(4096)
                connection.sendall(
                    b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                    b"4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n"
                )

    server = ChunkedServer()
    server.thread.start()
    try:
        status, body = tool.raw_http_get("127.0.0.1", server.port, "/command", 2, 64)
    finally:
        server.listener.close()
        server.thread.join(2)

    assert status == 200
    assert body == b"Wikipedia"


def test_stale_wait_deadline_does_not_misclassify_a_socket_timeout_as_server_close():
    tool = load_tool()
    client_socket, peer_socket = socket.socketpair()
    client = tool.RawWebSocket(client_socket, True, 101)
    try:
        assert client.wait_for_server_close(0.15) is False
    finally:
        client.close(graceful=False)
        peer_socket.close()


def test_stale_stage_uses_each_clients_actual_close_time_and_one_common_deadline():
    tool = load_tool()
    calls = 0
    calls_lock = threading.Lock()
    observe_barrier = threading.Barrier(2)

    class FakeStaleClient:
        accepted = True
        http_status = 101
        error = None
        ping_frames_received = 1
        last_ping_payload = b"FNC"

        def __init__(self, close_delay):
            self.close_delay = close_delay
            self.closed = False

        def wait_for_server_close(self, timeout):
            observe_barrier.wait(timeout=timeout)
            time.sleep(min(self.close_delay, timeout))
            return self.close_delay <= timeout

        def close(self, *, graceful):
            del graceful
            self.closed = True

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            nonlocal calls
            with calls_lock:
                index = calls
                calls += 1
            return FakeStaleClient(0.35 if index == 0 else 0.0)

    tool.RawWebSocket = FakeRawWebSocket
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        ws_timeout=0.1,
        stale_timeout=0.5,
        stale_min_close_seconds=0.3,
    )
    harness.capacity = 2
    harness.open_clients = []

    started = time.monotonic()
    try:
        with pytest.raises(tool.SoakAbort, match="too early"):
            harness._stage_stale(2)
    finally:
        harness._close_all()

    assert time.monotonic() - started < 0.55


def test_stale_stage_requires_a_real_firmware_ping_before_close_attribution():
    tool = load_tool()

    class FakeNoPingClient:
        accepted = True
        http_status = 101
        error = None
        ping_frames_received = 0
        last_ping_payload = None

        def wait_for_server_close(self, _timeout):
            return True

        def close(self, *, graceful):
            del graceful

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            return FakeNoPingClient()

    tool.RawWebSocket = FakeRawWebSocket
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        ws_timeout=0.1,
        stale_timeout=0.2,
        stale_min_close_seconds=0.0,
    )
    harness.capacity = 1
    harness.open_clients = []
    harness._wait_recovered = lambda _label: tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))

    with pytest.raises(tool.SoakAbort, match="without observing the firmware Ping/FNC heartbeat"):
        harness._stage_stale(1)


def test_churn_retries_attributed_heap_backpressure_after_a_quiet_window(monkeypatch):
    tool = load_tool()
    outcomes = iter((True, False, True))
    clients = []

    class FakeClient:
        def __init__(self, accepted):
            self.accepted = accepted
            self.http_status = 101 if accepted else 401
            self.error = None if accepted else "upgrade rejected"
            self.close_modes = []
            clients.append(self)

        def close(self, *, graceful):
            self.close_modes.append(graceful)

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            return FakeClient(next(outcomes))

    baseline = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    recovered = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Web WS heap rejections": 1}))
    )

    class FakeHttpClient:
        def capture_runtime_snapshot(self, _identity):
            return recovered

    class FakeRecorder:
        def __init__(self):
            self.events = []

        def event(self, name, **payload):
            self.events.append((name, payload))

    sleeps = []
    monkeypatch.setattr(tool, "RawWebSocket", FakeRawWebSocket)
    monkeypatch.setattr(tool.time, "sleep", sleeps.append)
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1)
    harness.baseline = baseline
    harness.http = FakeHttpClient()
    harness.recorder = FakeRecorder()

    stage = harness._stage_churn(2)

    assert stage["accepted"] == 2
    assert stage["rejected"] == 1
    assert stage["backpressureEvents"] == [
        {
            "cycle": 2,
            "httpStatus": 401,
            "quietSeconds": tool.CHURN_BACKPRESSURE_QUIET_SECONDS,
            "recovered": True,
        }
    ]
    assert sleeps == [tool.CHURN_BACKPRESSURE_QUIET_SECONDS]
    assert clients[0].close_modes == [True]
    assert clients[1].close_modes == [False]
    assert clients[2].close_modes == [False]


def test_churn_rejects_unattributed_single_client_refusal():
    tool = load_tool()

    class FakeRejectedClient:
        accepted = False
        http_status = 401
        error = "upgrade rejected"

        def close(self, *, graceful):
            del graceful

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            return FakeRejectedClient()

    baseline = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))

    class FakeHttpClient:
        def capture_runtime_snapshot(self, _identity):
            return baseline

    class FakeRecorder:
        def event(self, *_args, **_kwargs):
            pass

    tool.RawWebSocket = FakeRawWebSocket
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1)
    harness.baseline = baseline
    harness.http = FakeHttpClient()
    harness.recorder = FakeRecorder()

    with pytest.raises(tool.SoakAbort, match="not attributed"):
        harness._stage_churn(1)


def test_same_session_stage_allows_poll_free_channel_registration_before_replacement(monkeypatch):
    tool = load_tool()
    timeline = []
    clients = []

    class FakeClient:
        accepted = True
        http_status = 101
        error = None

        def __init__(self):
            self.server_closed = threading.Event()
            clients.append(self)

        def close(self, *, graceful):
            timeline.append(("close", graceful))

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **kwargs):
            timeline.append(("connect", kwargs.get("path")))
            client = FakeClient()
            if len(clients) == 2:
                clients[0].server_closed.set()
            return client

    active_snapshot = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Web WS active": 1}))
    )
    recovered_snapshot = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    monkeypatch.setattr(tool, "RawWebSocket", FakeRawWebSocket)
    monkeypatch.setattr(tool.time, "sleep", lambda seconds: timeline.append(("sleep", seconds)))
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1)
    harness.capacity = 2
    harness.open_clients = []
    harness._capture_guard = lambda _label: active_snapshot
    harness._wait_recovered = lambda _label: recovered_snapshot

    stage = harness._stage_same_session()

    assert stage["oldServerClosed"] is True
    assert timeline[:3] == [
        ("connect", "/"),
        ("sleep", tool.SAME_SESSION_REGISTRATION_SECONDS),
        ("connect", "/"),
    ]


def test_inter_stage_barrier_is_poll_free_and_requires_one_reconnect_probe(monkeypatch):
    tool = load_tool()
    timeline = []

    class FakeClient:
        accepted = True
        http_status = 101
        error = None

        def close(self, *, graceful):
            timeline.append(("close", graceful))

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            timeline.append(("connect", None))
            return FakeClient()

    class FakeRecorder:
        def event(self, name, **payload):
            timeline.append((name, payload))

    class ForbiddenHttpClient:
        def capture_runtime_snapshot(self, _identity):
            raise AssertionError("the quiet barrier must not poll FluidNC")

    monkeypatch.setattr(tool, "RawWebSocket", FakeRawWebSocket)
    monkeypatch.setattr(tool.time, "sleep", lambda seconds: timeline.append(("sleep", seconds)))
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        ws_timeout=0.1,
        inter_stage_quiet_seconds=12.5,
        post_recovery_quiet_seconds=65.0,
    )
    harness.http = ForbiddenHttpClient()
    harness.recorder = FakeRecorder()

    result = harness._quiet_transport_recovery("partial-to-parallel")

    assert result == {
        "label": "partial-to-parallel",
        "quietSeconds": 12.5,
        "rearmSeconds": 65.0,
        "reconnected": True,
    }
    assert timeline == [
        ("inter-stage-quiet", {"label": "partial-to-parallel", "quietSeconds": 12.5}),
        ("sleep", 12.5),
        ("connect", None),
        ("close", False),
        ("inter-stage-rearm", {"label": "partial-to-parallel", "quietSeconds": 65.0}),
        ("sleep", 65.0),
        ("inter-stage-recovered", result),
    ]


def test_aborted_upgrade_stage_requires_every_transport_to_send():
    tool = load_tool()
    calls = 0
    calls_lock = threading.Lock()

    def fake_abort(*_args, **_kwargs):
        nonlocal calls
        with calls_lock:
            index = calls
            calls += 1
        return {"sent": index != 7, "elapsedMs": 1, "error": "cutoff" if index == 7 else None}

    tool.abort_websocket_upgrade = fake_abort
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1, max_connections=8)

    with pytest.raises(tool.SoakAbort, match="7/8"):
        harness._stage_aborted_upgrades()


def test_aborted_upgrade_stage_uses_light_guard_before_heavy_recovery_observer(monkeypatch):
    tool = load_tool()
    timeline = []
    snapshot = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))

    monkeypatch.setattr(
        tool,
        "abort_websocket_upgrade",
        lambda *_args, **_kwargs: {"sent": True, "elapsedMs": 1, "error": None},
    )
    monkeypatch.setattr(tool.time, "sleep", lambda seconds: timeline.append(("sleep", seconds)))
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1, max_connections=8)
    harness._capture_light_state = lambda label: timeline.append(("light", label)) or "Idle"
    harness._capture_guard = lambda _label: pytest.fail("Heavy ESP420 observer ran before cleanup")
    harness._wait_recovered = lambda label, timeout: timeline.append(("recover", label, timeout)) or snapshot

    stage = harness._stage_aborted_upgrades()

    assert timeline == [
        ("light", "aborted-upgrades-reachable"),
        ("sleep", tool.IN_BAND_CLIENT_RELEASE_SECONDS),
        ("recover", "aborted-upgrades-cleanup", 15),
    ]
    assert stage["reachableState"] == "Idle"
    assert stage["recoveredPending"] == 0


def test_wait_recovered_retries_a_transient_heavy_observer_503(monkeypatch):
    tool = load_tool()
    baseline = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    calls = 0

    class FakeHttpClient:
        def capture_runtime_snapshot(self, _identity):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise tool.HttpStatusError(503, "/command?commandText=ESP420")
            return baseline

    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)
    harness = object.__new__(tool.SoakHarness)
    harness.http = FakeHttpClient()
    harness.baseline = baseline
    harness.last_snapshot = None
    harness.recorder = SimpleNamespace(event=lambda *_args, **_kwargs: None)

    recovered = harness._wait_recovered("transient-heavy", timeout=1)

    assert recovered is baseline
    assert calls == 2


def test_early_rst_stage_requires_every_transport_to_connect():
    tool = load_tool()
    calls = 0
    calls_lock = threading.Lock()

    def fake_reset(*_args, **_kwargs):
        nonlocal calls
        with calls_lock:
            index = calls
            calls += 1
        return {"connected": index != 7, "elapsedMs": 1, "error": "cutoff" if index == 7 else None}

    tool.abort_tcp_before_http = fake_reset
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=0.1, max_connections=8)
    snapshot = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    harness._capture_guard = lambda _label: snapshot

    with pytest.raises(tool.SoakAbort, match="7/8"):
        harness._stage_early_rst(1)


def test_partial_http_stage_requires_all_connections_and_closes_partial_successes():
    tool = load_tool()
    calls = 0
    calls_lock = threading.Lock()
    sockets = []

    class FakeSocket:
        def __init__(self):
            self.closed = False
            sockets.append(self)

        def shutdown(self, _how):
            pass

        def close(self):
            self.closed = True

    def fake_open(*_args, **_kwargs):
        nonlocal calls
        with calls_lock:
            index = calls
            calls += 1
        if index == 7:
            return None, "cutoff"
        return FakeSocket(), None

    tool.open_partial_http = fake_open
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, http_timeout=0.1, max_connections=8)
    harness.last_snapshot = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))

    with pytest.raises(tool.SoakAbort, match="7/8"):
        harness._stage_partial_http(1)

    assert len(sockets) == 7
    assert all(sock.closed for sock in sockets)


def test_partial_http_stage_holds_full_cap_but_samples_only_after_close(monkeypatch):
    tool = load_tool()
    sockets = []

    class FakeSocket:
        def __init__(self):
            self.closed = False

        def shutdown(self, _how):
            pass

        def close(self):
            self.closed = True

    def fake_open(*_args, **_kwargs):
        sock = FakeSocket()
        sockets.append(sock)
        return sock, None

    tool.open_partial_http = fake_open
    tool.partial_http_socket_pending = lambda sock: not sock.closed
    tool.probe_partial_http_hard_cap = lambda *_args: {
        "rejected": True,
        "httpStatus": 0,
        "responseBytes": 0,
        "error": "expected reset",
    }
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    before_stats = tool.parse_esp420(stats_payload())
    after_stats = dict(before_stats)
    after_stats["TCP PCBs active"] = 1
    after_stats["Async TCP accept admission rejections"] = 1
    before = tool.Snapshot.for_test(stats=before_stats)
    recovered = tool.Snapshot.for_test(stats=after_stats)
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        http_timeout=0.1,
        max_connections=8,
        partial_hold_seconds=2.0,
    )
    harness.last_snapshot = before

    captured = []

    def capture(label):
        if sockets:
            assert len(sockets) == 8
            assert all(sock.closed for sock in sockets), "in-band guard must not become a ninth live transport"
        captured.append(label)
        return recovered

    harness._capture_guard = capture
    harness._wait_recovered = lambda _label: recovered

    result = harness._stage_partial_http(1)

    assert result["openedTotal"] == 8
    assert result["samples"][0]["heldPending"] == 8
    assert result["samples"][0]["postCloseTcpActive"] == 1
    assert result["samples"][0]["acceptAdmissionRejectionDelta"] == 1
    assert result["samples"][0]["hardCapProbe"]["rejected"] is True
    assert captured == ["partial-http-1"]


def test_partial_http_stage_uses_prior_checkpoint_and_attributes_each_of_eleven_cap_probes(monkeypatch):
    tool = load_tool()
    sockets = []
    admission_rejections = 40
    operations = []
    open_calls = 0

    class FakeSocket:
        def __init__(self):
            self.closed = False

        def shutdown(self, _how):
            pass

        def close(self):
            self.closed = True

    def fake_open(*_args, **_kwargs):
        nonlocal open_calls
        open_calls += 1
        if open_calls % 8 == 1:
            operations.append(f"open-{(open_calls - 1) // 8 + 1}")
        sock = FakeSocket()
        sockets.append(sock)
        return sock, None

    def fake_cap_probe(*_args, **_kwargs):
        nonlocal admission_rejections
        admission_rejections += 1
        return {"rejected": True, "httpStatus": 0, "responseBytes": 0, "error": "expected reset"}

    tool.open_partial_http = fake_open
    tool.partial_http_socket_pending = lambda sock: not sock.closed
    tool.probe_partial_http_hard_cap = fake_cap_probe
    def fake_sleep(seconds):
        if seconds == tool.IN_BAND_CLIENT_RELEASE_SECONDS:
            operations.append("client-release-grace")

    monkeypatch.setattr(tool.time, "sleep", fake_sleep)

    def snapshot():
        return tool.Snapshot.for_test(
            stats=tool.parse_esp420(
                stats_payload(**{"Async TCP accept admission rejections": admission_rejections})
            )
        )

    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        http_timeout=0.1,
        max_connections=8,
        partial_hold_seconds=2.0,
    )
    harness.last_snapshot = snapshot()
    captured = []

    def capture(label):
        assert label != "partial-http-before", "the stage must reuse the prior poll-free checkpoint"
        assert all(sock.closed for sock in sockets), "telemetry must run only after every held transport closes"
        captured.append(label)
        operations.append(label)
        result = snapshot()
        harness.last_snapshot = result
        return result

    harness._capture_guard = capture
    def wait_recovered(label):
        operations.append(label)
        return snapshot()

    harness._wait_recovered = wait_recovered

    result = harness._stage_partial_http(11)

    assert result["openedTotal"] == 88
    assert len(result["samples"]) == 11
    assert all(sample["acceptAdmissionRejectionDelta"] == 1 for sample in result["samples"])
    assert captured == [f"partial-http-{index}" for index in range(1, 12)]
    round_ten_tail = operations[operations.index("partial-http-10") : operations.index("open-11") + 1]
    assert round_ten_tail == [
        "partial-http-10",
        "partial-http-cleanup-10",
        "client-release-grace",
        "open-11",
    ]


def test_partial_http_pending_probe_uses_existing_socket_without_consuming_data():
    tool = load_tool()
    client, peer = socket.socketpair()
    try:
        assert tool.partial_http_socket_pending(client)
        peer.sendall(b"x")
        assert not tool.partial_http_socket_pending(client)
        assert client.recv(1) == b"x"
        peer.close()
        assert not tool.partial_http_socket_pending(client)
    finally:
        client.close()
        peer.close()


def test_slow_static_get_enforces_one_deadline_across_connect_and_send(monkeypatch):
    tool = load_tool()
    clock_values = iter([100.0, 100.06, 100.061])

    class FakeSocket:
        def __init__(self):
            self.sent = False
            self.closed = False

        def settimeout(self, _timeout):
            pass

        def sendall(self, _payload):
            self.sent = True

        def getsockopt(self, _level, _option):
            return 8192

        def close(self):
            self.closed = True

    fake_socket = FakeSocket()

    def delayed_connect(*_args, **_kwargs):
        return fake_socket

    monkeypatch.setattr(tool.socket, "create_connection", delayed_connect)
    monkeypatch.setattr(tool.time, "monotonic", lambda: next(clock_values))

    result = tool.slow_static_get("127.0.0.1", 80, "/", 0.05, 0.0)

    assert result["status"] == 0
    assert "connect deadline" in result["error"]
    assert fake_socket.sent is False
    assert fake_socket.closed is True
    assert result["elapsedMs"] == 61


def test_slow_static_get_reports_body_bytes_and_signals_first_received_chunk():
    tool = load_tool()
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    port = listener.getsockname()[1]
    ready = threading.Event()
    release = threading.Event()
    release.set()

    def serve():
        connection, _address = listener.accept()
        try:
            connection.recv(4096)
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\ntest")
        finally:
            connection.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        result = tool.slow_static_get(
            "127.0.0.1",
            port,
            "/",
            2.0,
            0.0,
            first_chunk_event=ready,
            first_chunk_release_event=release,
            receive_buffer_bytes=1024,
        )
    finally:
        listener.close()
        thread.join(2)

    assert ready.is_set()
    assert result["status"] == 200
    assert result["bodyBytes"] == 4
    assert result["declaredBodyBytes"] == 4
    assert result["complete"] is True
    assert result["receiveBufferBytes"] >= 1024


def test_static_socket_sets_receive_buffer_before_connect(monkeypatch):
    tool = load_tool()
    operations = []

    class FakeSocket:
        def setsockopt(self, level, option, value):
            operations.append(("setsockopt", level, option, value))

        def settimeout(self, value):
            operations.append(("settimeout", value))

        def connect(self, address):
            operations.append(("connect", address))

        def getsockopt(self, level, option):
            operations.append(("getsockopt", level, option))
            return 4096

        def close(self):
            operations.append(("close",))

    fake_socket = FakeSocket()
    monkeypatch.setattr(
        tool.socket,
        "getaddrinfo",
        lambda *_args, **_kwargs: [(tool.socket.AF_INET, tool.socket.SOCK_STREAM, 0, "", ("127.0.0.1", 80))],
    )
    monkeypatch.setattr(tool.socket, "socket", lambda *_args: fake_socket)

    connected, actual = tool.connect_static_socket("127.0.0.1", 80, 1.0, 1024)

    assert connected is fake_socket
    assert actual == 4096
    assert [operation[0] for operation in operations] == ["setsockopt", "settimeout", "connect", "getsockopt"]


def test_slow_static_get_marks_truncated_content_length_response_incomplete():
    tool = load_tool()
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    port = listener.getsockname()[1]

    def serve():
        connection, _address = listener.accept()
        try:
            connection.recv(4096)
            connection.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\nConnection: close\r\n\r\nx")
        finally:
            connection.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        result = tool.slow_static_get("127.0.0.1", port, "/", 2.0, 0.0)
    finally:
        listener.close()
        thread.join(2)

    assert result["status"] == 200
    assert result["bodyBytes"] == 1
    assert result["declaredBodyBytes"] == 10
    assert result["complete"] is False


def test_static_stage_rejects_a_nominal_200_without_file_lifecycle_delta(monkeypatch):
    tool = load_tool()
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    def fake_static(*_args, **kwargs):
        event = kwargs.get("first_chunk_event")
        if event is not None:
            event.set()
        return {
            "path": "/",
            "status": 200,
            "bytes": 128,
            "bodyBytes": 64,
            "declaredBodyBytes": 64,
            "complete": True,
            "elapsedMs": 1,
            "error": None,
        }

    tool.slow_static_get = fake_static
    inactive = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        http_timeout=1.0,
        static_read_delay=0.0,
    )
    harness.capacity = 0
    harness.open_clients = []
    harness.http = SimpleNamespace(command=lambda command: b"State 0 (Idle)\n" if command == "$State" else b"")
    harness.recorder = SimpleNamespace(event=lambda *_args, **_kwargs: None)
    harness._capture_guard = lambda _label: inactive
    harness._wait_recovered = lambda _label: inactive

    with pytest.raises(tool.SoakAbort, match="did not produce exactly one file-stream lifecycle"):
        harness._stage_static(1)


def test_static_stage_rejects_truncated_positive_transfer(monkeypatch):
    tool = load_tool()

    def fake_static(*_args, **kwargs):
        event = kwargs.get("first_chunk_event")
        if event is not None:
            event.set()
        return {
            "path": "/",
            "status": 200,
            "bytes": 128,
            "bodyBytes": 1,
            "declaredBodyBytes": 64,
            "complete": False,
            "elapsedMs": 1,
            "error": None,
        }

    tool.slow_static_get = fake_static
    active = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Web file streams": 1}))
    )
    inactive = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, http_timeout=1.0, static_read_delay=0.0)
    harness.capacity = 0
    harness.open_clients = []
    harness.http = SimpleNamespace(command=lambda _command: b"State 0 (Idle)\n")
    harness.recorder = SimpleNamespace(event=lambda *_args, **_kwargs: None)
    harness._capture_guard = lambda _label: active
    harness._wait_recovered = lambda _label: inactive

    with pytest.raises(tool.SoakAbort, match="complete HTTP 200"):
        harness._stage_static(1)


def test_static_stage_recovers_one_websocket_then_rediscoveres_degraded_capacity(monkeypatch):
    tool = load_tool()
    operations = []

    def fake_static(*_args, **kwargs):
        operations.append("static-get")
        first_chunk_event = kwargs.get("first_chunk_event")
        release_event = kwargs.get("first_chunk_release_event")
        if first_chunk_event is not None:
            first_chunk_event.set()
            assert release_event.wait(1)
        return {
            "path": "/",
            "status": 200,
            "bytes": 128,
            "bodyBytes": 64,
            "declaredBodyBytes": 64,
            "complete": True,
            "receiveBufferBytes": 4096,
            "elapsedMs": 1,
            "error": None,
        }

    class FakeClient:
        def __init__(self, accepted):
            self.accepted = accepted
            self.http_status = 101 if accepted else 503
            self.error = None if accepted else "heap policy"
            self.closed = False

        def close(self, *, graceful):
            del graceful
            self.closed = True

    clients = [FakeClient(True), FakeClient(False)]

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            operations.append("ws-connect")
            return clients.pop(0)

    tool.slow_static_get = fake_static
    tool.RawWebSocket = FakeRawWebSocket
    monkeypatch.setattr(tool.time, "sleep", lambda seconds: operations.append(("sleep", seconds)))
    inactive = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    cleaned = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Web file starts": 1, "Web file completions": 1}))
    )
    one_websocket = tool.Snapshot.for_test(
        stats=tool.parse_esp420(
            stats_payload(**{"Web WS active": 1, "Web file starts": 1, "Web file completions": 1})
        )
    )
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        http_timeout=1.0,
        ws_timeout=1.0,
        static_read_delay=0.0,
        max_connections=2,
    )
    harness.capacity = 2
    harness.open_clients = []
    harness.recorder = SimpleNamespace(event=lambda *args, **kwargs: operations.append((args, kwargs)))
    harness.http = SimpleNamespace(command=lambda _command: b"State 0 (Idle)\n")

    def capture(label):
        operations.append(label)
        if label.endswith("-rediscovered"):
            return one_websocket
        return inactive

    harness._capture_guard = capture
    harness._wait_recovered = lambda _label: cleaned

    result = harness._stage_static(0)

    first_connect = operations.index("ws-connect")
    assert operations[first_connect - 1] == ("sleep", tool.CHURN_BACKPRESSURE_QUIET_SECONDS)
    static_before = operations.index("static-before")
    assert operations[static_before : static_before + 3] == [
        "static-before",
        ("sleep", tool.IN_BAND_CLIENT_RELEASE_SECONDS),
        "static-get",
    ]
    assert result["initialDiscoveredCapacity"] == 2
    assert result["currentCapacity"] == 1
    assert result["capacityDegradation"] == 1


def test_static_stage_aborts_when_poll_free_recovery_cannot_admit_one_websocket(monkeypatch):
    tool = load_tool()

    def fake_static(*_args, **kwargs):
        first_chunk_event = kwargs.get("first_chunk_event")
        release_event = kwargs.get("first_chunk_release_event")
        if first_chunk_event is not None:
            first_chunk_event.set()
            assert release_event.wait(1)
        return {
            "path": "/",
            "status": 200,
            "bytes": 128,
            "bodyBytes": 64,
            "declaredBodyBytes": 64,
            "complete": True,
            "receiveBufferBytes": 1024,
            "elapsedMs": 1,
            "error": None,
        }

    class RejectedClient:
        accepted = False
        http_status = 401
        error = "heap policy"

        def close(self, *, graceful):
            del graceful

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            return RejectedClient()

    tool.slow_static_get = fake_static
    tool.RawWebSocket = FakeRawWebSocket
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)
    inactive = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    cleaned = tool.Snapshot.for_test(
        stats=tool.parse_esp420(stats_payload(**{"Web file starts": 1, "Web file completions": 1}))
    )
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(
        host="127.0.0.1",
        port=80,
        http_timeout=1.0,
        ws_timeout=1.0,
        static_read_delay=0.0,
        max_connections=2,
    )
    harness.capacity = 2
    harness.open_clients = []
    harness.recorder = SimpleNamespace(event=lambda *_args, **_kwargs: None)
    harness.http = SimpleNamespace(command=lambda _command: b"State 0 (Idle)\n")
    harness._capture_guard = lambda _label: inactive
    harness._wait_recovered = lambda _label: cleaned

    with pytest.raises(tool.SoakAbort, match="did not regain even one WebSocket"):
        harness._stage_static(0)


def test_parallel_connect_exception_harvests_and_closes_late_successes():
    tool = load_tool()
    lock = threading.Lock()
    calls = 0
    accepted = []

    class Client:
        def __init__(self):
            self.closed = False
            accepted.append(self)

        def close(self, *, graceful):
            assert graceful is False
            self.closed = True

    class FakeRawWebSocket:
        @classmethod
        def connect(cls, *_args, **_kwargs):
            nonlocal calls
            with lock:
                index = calls
                calls += 1
            if index == 0:
                raise RuntimeError("early future failure")
            time.sleep(0.1)
            return Client()

    tool.RawWebSocket = FakeRawWebSocket
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(host="127.0.0.1", port=80, ws_timeout=1.0)

    with pytest.raises(RuntimeError, match="early future failure"):
        harness._parallel_connect(2)

    assert len(accepted) == 1
    assert accepted[0].closed is True


def test_parallel_repro_profile_runs_only_the_parallel_stage(monkeypatch):
    tool = load_tool()
    baseline = tool.Snapshot.for_test(stats=tool.parse_esp420(stats_payload()))
    calls = []
    finished = []
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(profile="parallel-repro", expected_hardening_id="r17")
    harness.http = SimpleNamespace(capture_snapshot=lambda: baseline)
    harness.recorder = SimpleNamespace(
        report={"stages": []},
        event=lambda *_args, **_kwargs: None,
        finish=lambda status, **fields: finished.append((status, fields)),
    )
    harness.capacity = 0
    harness.baseline = None
    harness.last_snapshot = None
    harness._stage_parallel = lambda cycles: calls.append(("parallel", cycles)) or {
        "name": "parallel-handshakes",
        "cycles": cycles,
    }
    harness._capture_full_guard = lambda label: calls.append(("identity", label)) or baseline
    harness._wait_recovered = lambda label, timeout=20.0: calls.append(("recovered", label, timeout)) or baseline
    harness._wait_postflight_stable = lambda identity: calls.append(("postflight", identity)) or identity
    harness._quiet_transport_recovery = lambda label: pytest.fail(f"unexpected inter-stage quiet recovery: {label}")
    monkeypatch.setattr(tool, "validate_preflight", lambda *_args: [])
    monkeypatch.setattr(tool, "validate_continuity", lambda *_args: [])
    monkeypatch.setattr(tool, "_resource_errors", lambda *_args: [])

    harness.run()

    assert calls == [
        ("parallel", tool.PROFILE["parallel-repro"]["parallelCycles"]),
        ("identity", "parallel-handshakes-identity"),
        ("recovered", "final-recovery", 30),
        ("postflight", baseline),
    ]
    assert harness.recorder.report["stages"] == [
        {"name": "parallel-handshakes", "cycles": tool.PROFILE["parallel-repro"]["parallelCycles"]}
    ]
    assert finished == [("PASS", {"capacity": 0, "final": baseline.to_dict()})]


def test_parallel_http000_records_structured_failure_before_aborting(monkeypatch):
    tool = load_tool()
    events = []
    baseline = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 10,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 4,
            "Async TCP accept admission rejections": 7,
        }
    )
    after = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 10,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 5,
            "Async TCP accept admission rejections": 8,
        }
    )
    client = SimpleNamespace(
        accepted=False,
        http_status=0,
        error="peer closed before HTTP status",
        failure_kind="eof_before_http_status",
        handshake_response_bytes=0,
        handshake_elapsed_ms=3087,
    )
    snapshots = iter([baseline, after])
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(max_connections=1)
    harness.open_clients = []
    harness.recorder = SimpleNamespace(event=lambda name, **fields: events.append((name, fields)))
    harness._capture_guard = lambda _label: next(snapshots)
    harness._parallel_connect = lambda _count: [client]
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    with pytest.raises(tool.SoakAbort, match="unattributed HTTP000/transport failure"):
        harness._stage_parallel(1)

    assert events == [
        (
            "parallel-http000",
            {
                "cycle": 1,
                "failures": [
                    {
                        "status": 0,
                        "kind": "eof_before_http_status",
                        "error": "peer closed before HTTP status",
                        "responseBytes": 0,
                        "elapsedMs": 3087,
                    }
                ],
                "rejectionDelta": 0,
                "rxTimeoutDelta": 1,
                "acceptAdmissionRejectionDelta": 1,
            },
        )
    ]


def test_parallel_bounded_peer_abort_with_exact_policy_counter_is_an_attributed_rejection(monkeypatch):
    tool = load_tool()
    events = []
    baseline = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 10,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 4,
            "Async TCP accept admission rejections": 7,
        }
    )
    after = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 11,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 4,
            "Async TCP accept admission rejections": 7,
        }
    )
    client = SimpleNamespace(
        accepted=False,
        http_status=0,
        error="ConnectionResetError: peer reset",
        failure_kind="peer_transport_abort",
        handshake_response_bytes=0,
        # Measured on the diagnostic board under an eight-way simultaneous
        # handshake: the allocation-free policy abort was counter-attributed
        # but arrived after 1.077s of queued TCP work, still far below the
        # five-second client deadline.
        handshake_elapsed_ms=1077,
    )
    snapshots = iter([baseline, after])
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(max_connections=1, ws_timeout=5.0)
    harness.open_clients = []
    harness.recorder = SimpleNamespace(event=lambda name, **fields: events.append((name, fields)))
    harness._capture_guard = lambda _label: next(snapshots)
    harness._parallel_connect = lambda _count: [client]
    harness._wait_recovered = lambda _label: after
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    stage = harness._stage_parallel(1)

    assert stage["cycles"] == 1
    assert events == [
        (
            "parallel-attributed-transport-rejection",
            {
                "cycle": 1,
                "failures": [
                    {
                        "status": 0,
                        "kind": "peer_transport_abort",
                        "error": "ConnectionResetError: peer reset",
                        "responseBytes": 0,
                        "elapsedMs": 1077,
                    }
                ],
                "rejectionDelta": 1,
            },
        )
    ]


def test_parallel_late_peer_abort_remains_a_hard_failure_even_with_policy_counter(monkeypatch):
    tool = load_tool()
    baseline = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 10,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 4,
            "Async TCP accept admission rejections": 7,
        }
    )
    after = SimpleNamespace(
        stats={
            "Web WS active": 0,
            "Web WS heap rejections": 11,
            "Web WS limit rejections": 0,
            "Async TCP RX timeouts": 4,
            "Async TCP accept admission rejections": 7,
        }
    )
    client = SimpleNamespace(
        accepted=False,
        http_status=0,
        error="ConnectionResetError: peer reset",
        failure_kind="peer_transport_abort",
        handshake_response_bytes=0,
        handshake_elapsed_ms=1501,
    )
    snapshots = iter([baseline, after])
    harness = object.__new__(tool.SoakHarness)
    harness.args = Namespace(max_connections=1, ws_timeout=5.0)
    harness.open_clients = []
    harness.recorder = SimpleNamespace(event=lambda *_args, **_kwargs: None)
    harness._capture_guard = lambda _label: next(snapshots)
    harness._parallel_connect = lambda _count: [client]
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    with pytest.raises(tool.SoakAbort, match="unattributed HTTP000/transport failure"):
        harness._stage_parallel(1)


def test_live_cli_requires_explicit_execution_and_expected_build(tmp_path):
    tool = load_tool()
    output = tmp_path / "new-evidence"

    with pytest.raises(SystemExit):
        tool.parse_args(["--host", "192.0.2.1", "--output", str(output)])

    args = tool.parse_args(
        [
            "--host",
            "192.0.2.1",
            "--output",
            str(output),
            "--profile",
            "parallel-repro",
            "--execute-live",
            "--expected-hardening-id",
            HARDENING_ID,
        ]
    )
    assert args.execute_live is True
    assert args.profile == "parallel-repro"
    assert args.expected_hardening_id == HARDENING_ID


def test_keyboard_interrupt_persists_abort_report_cleans_up_and_is_re_raised(tmp_path):
    tool = load_tool()
    cleanup_called = threading.Event()

    class InterruptHarness:
        capacity = 0

        def __init__(self, _args, _recorder):
            pass

        def run(self):
            raise KeyboardInterrupt("operator stop")

        def cleanup(self):
            cleanup_called.set()

    tool.SoakHarness = InterruptHarness
    output = tmp_path / "interrupt-evidence"

    with pytest.raises(KeyboardInterrupt, match="operator stop"):
        tool.main(
            [
                "--host",
                "192.0.2.1",
                "--output",
                str(output),
                "--execute-live",
                "--expected-hardening-id",
                HARDENING_ID,
            ]
        )

    report = json.loads((output / "soak-report.json").read_text(encoding="utf-8"))
    assert report["status"] == "ABORT"
    assert report["errorType"] == "KeyboardInterrupt"
    assert cleanup_called.is_set()


def test_complete_smoke_state_machine_passes_against_deterministic_fake_fluidnc(tmp_path, monkeypatch):
    tool = load_tool()
    monkeypatch.setattr(tool.time, "sleep", lambda _seconds: None)

    class FakeBoard:
        def __init__(self):
            self.lock = threading.Lock()
            self.active = 0
            self.heap_rejections = 0
            self.limit_rejections = 0
            self.file_rejections = 0
            self.file_starts = 0
            self.file_completions = 0
            self.early_rst_cleanups = 0
            self.accept_rejections = 0
            self.file_streams = 0
            self.same_session = None

        def snapshot(self):
            with self.lock:
                stats = tool.parse_esp420(
                    stats_payload(
                        **{
                            "Web WS active": self.active,
                            "Web WS heap rejections": self.heap_rejections,
                            "Web WS limit rejections": self.limit_rejections,
                            "Web file rejections": self.file_rejections,
                            "Web file starts": self.file_starts,
                            "Web file completions": self.file_completions,
                            "Async TCP early RST accept cleanups": self.early_rst_cleanups,
                            "Async TCP accept admission rejections": self.accept_rejections,
                            "Web file streams": self.file_streams,
                        }
                    )
                )
            return tool.Snapshot.for_test(stats=stats)

    board = FakeBoard()

    class FakeHttpClient:
        def __init__(self, *_args, **_kwargs):
            pass

        def capture_snapshot(self):
            return board.snapshot()

        def capture_runtime_snapshot(self, _identity):
            return board.snapshot()

        def command(self, command):
            assert command == "$State"
            return b"State 0 (Idle)\n"

    class FakeWebSocket:
        application_frames_sent = 0

        def __init__(self, accepted):
            self.accepted = accepted
            self.http_status = 101 if accepted else 503
            self.error = None if accepted else "policy"
            self.closed = not accepted
            self.server_closed = threading.Event()
            self.ping_frames_received = 0
            self.last_ping_payload = None

        @classmethod
        def connect(cls, *_args, **kwargs):
            with board.lock:
                if kwargs.get("path") == "/" and board.same_session is not None:
                    board.same_session.server_closed.set()
                    board.same_session.closed = True
                    client = cls(True)
                    board.same_session = client
                    return client
                if board.active < 3:
                    board.active += 1
                    client = cls(True)
                    if kwargs.get("path") == "/":
                        board.same_session = client
                    return client
                board.heap_rejections += 1
                return cls(False)

        def wait_for_server_close(self, _timeout):
            self.ping_frames_received = 1
            self.last_ping_payload = b"FNC"
            self.close(graceful=False)
            return True

        def close(self, *, graceful=True):
            del graceful
            if self.accepted and not self.closed:
                with board.lock:
                    board.active -= 1
                    if board.same_session is self:
                        board.same_session = None
                self.closed = True

    tool.FluidNCReadOnlyClient = FakeHttpClient
    tool.RawWebSocket = FakeWebSocket
    tool.abort_websocket_upgrade = lambda *_args, **_kwargs: {"sent": True, "elapsedMs": 1, "error": None}
    def fake_early_rst(*_args, **_kwargs):
        with board.lock:
            board.early_rst_cleanups += 1
        return {"connected": True, "elapsedMs": 1, "error": None}

    tool.abort_tcp_before_http = fake_early_rst

    class FakePartialSocket:
        def shutdown(self, _how):
            pass

        def close(self):
            pass

    tool.open_partial_http = lambda *_args, **_kwargs: (FakePartialSocket(), None)
    tool.partial_http_socket_pending = lambda _sock: True

    def fake_cap_probe(*_args, **_kwargs):
        with board.lock:
            board.accept_rejections += 1
        return {"rejected": True, "httpStatus": 0, "responseBytes": 0, "error": "expected reset"}

    tool.probe_partial_http_hard_cap = fake_cap_probe
    def fake_static_get(_host, _port, path, _timeout, _delay, **kwargs):
        first_chunk_event = kwargs.get("first_chunk_event")
        first_chunk_release_event = kwargs.get("first_chunk_release_event")
        if first_chunk_event is not None:
            with board.lock:
                board.file_streams = 1
                board.file_starts += 1
            first_chunk_event.set()
            if first_chunk_release_event is not None:
                assert first_chunk_release_event.wait(1)
            with board.lock:
                board.file_streams = 0
                board.file_completions += 1
        return {
            "path": path,
            "status": 200,
            "bytes": 1024,
            "bodyBytes": 900,
            "declaredBodyBytes": 900,
            "complete": True,
            "receiveBufferBytes": 4096,
            "elapsedMs": 1,
            "error": None,
        }

    tool.slow_static_get = fake_static_get
    args = Namespace(
        host="127.0.0.1",
        port=80,
        output=tmp_path / "fake-soak",
        profile="smoke",
        max_connections=8,
        http_timeout=1.0,
        ws_timeout=1.0,
        stale_timeout=1.0,
        stale_min_close_seconds=0.0,
        partial_hold_seconds=0.001,
            static_read_delay=0.0,
            inter_stage_quiet_seconds=0.0,
            post_recovery_quiet_seconds=0.0,
            expected_hardening_id=HARDENING_ID,
        execute_live=True,
    )
    recorder = tool.EvidenceRecorder(args.output, {"fake": True})
    harness = tool.SoakHarness(args, recorder)

    harness.run()

    report = json.loads(recorder.report_path.read_text(encoding="utf-8"))
    assert report["status"] == "PASS"
    assert report["capacity"] == 3
    assert [stage["name"] for stage in report["stages"]] == [
        "sequential-ramp",
        "same-session-newest-wins",
        "connect-close-churn",
        "tcp-rst-before-dispatch",
        "aborted-upgrade-recovery",
        "partial-http-recovery",
        "parallel-handshakes",
        "no-pong-stale-eviction",
        "mixed-ws-static-get-pressure",
    ]
    assert board.active == 0
