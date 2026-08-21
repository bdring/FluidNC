#!/usr/bin/env python3
"""Fail-closed, read-only FluidNC Web/WS resource soak harness.

The live mode deliberately permits only HTTP GETs, RFC6455 handshakes/control
frames, and TCP close.  It never sends WebSocket application data and has no
reset, motion, G-code, macro, upload, or configuration-write code path.
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
import re
import select
import socket
import struct
import threading
import time
import traceback
import urllib.parse
from pathlib import Path
from typing import Any, Iterable


WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
ASYNC_TCP_HARD_CAP = 8
# ESP420 is itself a guarded Heavy-HTTP response.  Its snapshot is taken after
# that response has reserved its slot and allocated its encoder state, so it
# cannot truthfully expose the raw heap a subsequent WebSocket handshake will
# observe after the HTTP connection is reaped.  Keep the preflight bounded at
# the normal first-socket policy floor and let the actual handshake admission
# (after a poll-free release grace) prove the raw/effective headroom.  A 401 is
# an expected fail-closed capacity result, never permission to lower firmware
# resource floors.
PREFLIGHT_IN_BAND_OBSERVER_MIN_BYTES = 28 * 1024
BYTE_FIELDS = {
    "Free memory",
    "Largest free block",
    "Heap allocated bytes",
    "Heap minimum free",
    "Web WS last observed free",
    "Web WS last largest block",
    "Web WS last effective free",
    "Async TCP accept last observed free",
    "Async TCP accept last largest block",
    "Async TCP accept last effective free",
}
RESOURCE_ZERO_FIELDS = (
    "Web WS pending",
    "Web WS active",
    "Web WS connecting",
    "Web WS deferred closes",
    "Web file streams",
    "Async TCP server pending accepts",
)
RESOURCE_IN_BAND_FIELDS = {
    # Every Snapshot is serialized by [ESP420] itself.  r15 deliberately owns
    # one Heavy-HTTP slot for that response until the connection is reaped.
    # If an earlier Heavy owner leaked, this observer cannot be admitted and
    # capture_snapshot fails with HTTP 503 before reaching this assertion.
    "Web heavy HTTP active": 1,
}
REQUIRED_STATS_FIELDS = {
    "Diagnostic hardening ID",
    "Diagnostic boot sequence",
    "Diagnostic uptime ms",
    "Diagnostic reset reason",
    "Free memory",
    "Largest free block",
    "Heap minimum free",
    *RESOURCE_ZERO_FIELDS,
    *RESOURCE_IN_BAND_FIELDS,
    "Web WS limit rejections",
    "Web WS heap rejections",
    "Web WS recovery admissions",
    "Web WS zero idle ms",
    "Web file starts",
    "Web file completions",
    "Web file rejections",
    "Web heavy HTTP rejections",
    "Web requests created",
    "Web requests destroyed",
    "Web request owners allocated",
    "Web request owners deallocated",
    "Async TCP clients created",
    "Async TCP clients destroyed",
    "Async TCP events created",
    "Async TCP events destroyed",
    "Async TCP event queue depth",
    "Async TCP event queue high water",
    "Async TCP RX timeouts",
    "Async TCP accept event allocation failures",
    "Async TCP early RST accept cleanups",
    "Async TCP accept admission rejections",
    "Async TCP server pending accepts",
    "Async TCP accept last observed free",
    "Async TCP accept last largest block",
    "Async TCP accept last effective free",
    "Async TCP accept last live clients",
    "Async TCP accept callbacks",
    "Async TCP accept null PCBs",
    "Async TCP accept last null PCB error",
    "Async TCP accept client allocation failures",
    "Async TCP accept client setup failures",
    "Async WebSocket reject abort calls",
    "Async WebSocket reject abort max us",
    "TCP PCBs active",
    "TCP PCBs time wait",
    "TCP accept PCB active+TIME_WAIT peak",
    "TCP listener backlog",
    "TCP listener accepts pending",
}
IDENTITY_ATTRIBUTES = (
    "hardening_id",
    "boot_sequence",
    "build_sha256",
    "firmware_sha256",
    "config_filename",
    "config_sha256",
    "runtime_sha256",
    "backtrace_sha256",
    "journal_sha256",
)


class SoakAbort(RuntimeError):
    pass


class HttpStatusError(SoakAbort):
    def __init__(self, status: int, path: str):
        self.status = status
        self.path = path
        super().__init__(f"unexpected HTTP {status} for read-only GET {path}")


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def parse_byte_value(value: str) -> int:
    match = re.fullmatch(r"\s*([0-9]+(?:\.[0-9]+)?)\s*(B|KB|MB)\s*", value, re.IGNORECASE)
    if not match:
        raise ValueError(f"unsupported FluidNC byte value: {value!r}")
    multiplier = {"B": 1, "KB": 1024, "MB": 1024 * 1024}[match.group(2).upper()]
    return int(round(float(match.group(1)) * multiplier))


def parse_esp420(payload: bytes | str) -> dict[str, Any]:
    if isinstance(payload, bytes):
        payload = payload.decode("utf-8-sig")
    document = json.loads(payload)
    if document.get("status") != "ok" or not isinstance(document.get("data"), list):
        raise ValueError("ESP420 response is not an ok id/value list")
    result: dict[str, Any] = {}
    for entry in document["data"]:
        key = entry.get("id")
        value = entry.get("value")
        if not isinstance(key, str):
            continue
        if key in BYTE_FIELDS and isinstance(value, str):
            result[key] = parse_byte_value(value)
        elif isinstance(value, str) and re.fullmatch(r"-?[0-9]+", value.strip()):
            result[key] = int(value)
        else:
            result[key] = value
    return result


def build_websocket_handshake(host: str, port: int, target: str, key: str) -> bytes:
    if not target.startswith("/") or "\r" in target or "\n" in target:
        raise SoakAbort(f"unsafe WebSocket request target: {target!r}")
    return (
        f"GET {target} HTTP/1.1\r\n"
        f"Host: {host}:{port}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: FluidNC-read-only-soak/1\r\n\r\n"
    ).encode("ascii")


def _recv_with_deadline(sock: socket.socket, deadline: float) -> bytes:
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise SoakAbort("hard HTTP deadline exceeded")
    sock.settimeout(remaining)
    try:
        return sock.recv(8192)
    except socket.timeout as error:
        raise SoakAbort("hard HTTP deadline exceeded") from error


def _take_until(sock: socket.socket, buffer: bytearray, delimiter: bytes, deadline: float, maximum: int) -> bytes:
    while True:
        index = buffer.find(delimiter)
        if index >= 0:
            value = bytes(buffer[:index])
            del buffer[: index + len(delimiter)]
            return value
        if len(buffer) > maximum:
            raise SoakAbort(f"HTTP field exceeded {maximum} bytes")
        chunk = _recv_with_deadline(sock, deadline)
        if not chunk:
            raise SoakAbort("HTTP peer closed before a complete response")
        buffer.extend(chunk)


def _take_exact(sock: socket.socket, buffer: bytearray, size: int, deadline: float) -> bytes:
    while len(buffer) < size:
        chunk = _recv_with_deadline(sock, deadline)
        if not chunk:
            raise SoakAbort("HTTP peer closed before the declared response body completed")
        buffer.extend(chunk)
    value = bytes(buffer[:size])
    del buffer[:size]
    return value


def raw_http_get(host: str, port: int, path: str, timeout: float, limit: int) -> tuple[int, bytes]:
    deadline = time.monotonic() + timeout
    sock: socket.socket | None = None
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SoakAbort("hard HTTP connect deadline exceeded")
        sock.settimeout(remaining)
        sock.sendall(
            (
                f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nConnection: close\r\n"
                "User-Agent: FluidNC-read-only-soak/1\r\n\r\n"
            ).encode("ascii")
        )
        buffer = bytearray()
        header_blob = _take_until(sock, buffer, b"\r\n\r\n", deadline, 32 * 1024)
        lines = header_blob.split(b"\r\n")
        status_match = re.match(rb"HTTP/1\.[01]\s+([0-9]{3})(?:\s|$)", lines[0] if lines else b"")
        if not status_match:
            raise SoakAbort("invalid HTTP status line")
        status = int(status_match.group(1))
        headers: dict[str, str] = {}
        for line in lines[1:]:
            if b":" not in line:
                raise SoakAbort("malformed HTTP response header")
            name, value = line.split(b":", 1)
            headers[name.decode("ascii", "strict").lower()] = value.decode("ascii", "strict").strip()

        body = bytearray()
        if status not in (204, 304) and not 100 <= status < 200:
            transfer_encoding = headers.get("transfer-encoding", "").lower()
            content_length = headers.get("content-length")
            if "chunked" in transfer_encoding:
                while True:
                    size_line = _take_until(sock, buffer, b"\r\n", deadline, 8192)
                    try:
                        chunk_size = int(size_line.split(b";", 1)[0], 16)
                    except ValueError as error:
                        raise SoakAbort(f"invalid HTTP chunk size: {size_line!r}") from error
                    if chunk_size == 0:
                        while _take_until(sock, buffer, b"\r\n", deadline, 8192):
                            pass
                        break
                    if len(body) + chunk_size > limit:
                        raise SoakAbort(f"read-only response exceeded {limit} bytes for {path}")
                    body.extend(_take_exact(sock, buffer, chunk_size, deadline))
                    if _take_exact(sock, buffer, 2, deadline) != b"\r\n":
                        raise SoakAbort("HTTP chunk lacked terminating CRLF")
            elif content_length is not None:
                try:
                    declared = int(content_length)
                except ValueError as error:
                    raise SoakAbort(f"invalid HTTP Content-Length: {content_length!r}") from error
                if declared < 0 or declared > limit:
                    raise SoakAbort(f"read-only response exceeded {limit} bytes for {path}")
                body.extend(_take_exact(sock, buffer, declared, deadline))
            else:
                body.extend(buffer)
                if len(body) > limit:
                    raise SoakAbort(f"read-only response exceeded {limit} bytes for {path}")
                while True:
                    chunk = _recv_with_deadline(sock, deadline)
                    if not chunk:
                        break
                    body.extend(chunk)
                    if len(body) > limit:
                        raise SoakAbort(f"read-only response exceeded {limit} bytes for {path}")
        return status, bytes(body)
    except UnicodeError as error:
        raise SoakAbort(f"non-ASCII HTTP response metadata for {path}") from error
    except OSError as error:
        raise SoakAbort(f"read-only GET failed for {path}: {error}") from error
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


class Snapshot:
    def __init__(
        self,
        *,
        captured_utc: str,
        state: str,
        stats: dict[str, Any],
        build_sha256: str,
        firmware_sha256: str,
        config_filename: str,
        config_sha256: str,
        runtime_sha256: str,
        backtrace_sha256: str,
        journal_sha256: str,
    ):
        self.captured_utc = captured_utc
        self.state = state
        self.stats = stats
        self.hardening_id = stats.get("Diagnostic hardening ID")
        self.boot_sequence = stats.get("Diagnostic boot sequence")
        self.uptime_ms = stats.get("Diagnostic uptime ms")
        self.reset_reason = stats.get("Diagnostic reset reason")
        self.build_sha256 = build_sha256
        self.firmware_sha256 = firmware_sha256
        self.config_filename = config_filename
        self.config_sha256 = config_sha256
        self.runtime_sha256 = runtime_sha256
        self.backtrace_sha256 = backtrace_sha256
        self.journal_sha256 = journal_sha256

    @classmethod
    def for_test(
        cls,
        *,
        state: str = "Idle",
        stats: dict[str, Any] | None = None,
        build_sha256: str = "build",
        firmware_sha256: str = "firmware",
        config_filename: str = "config.yaml",
        config_sha256: str = "config",
        runtime_sha256: str = "runtime",
        backtrace_sha256: str = "backtrace",
        journal_sha256: str = "journal",
    ) -> "Snapshot":
        return cls(
            captured_utc=utc_now(),
            state=state,
            stats=stats or {},
            build_sha256=build_sha256,
            firmware_sha256=firmware_sha256,
            config_filename=config_filename,
            config_sha256=config_sha256,
            runtime_sha256=runtime_sha256,
            backtrace_sha256=backtrace_sha256,
            journal_sha256=journal_sha256,
        )

    def to_dict(self) -> dict[str, Any]:
        return {
            "capturedUtc": self.captured_utc,
            "state": self.state,
            "hardeningId": self.hardening_id,
            "bootSequence": self.boot_sequence,
            "uptimeMs": self.uptime_ms,
            "resetReason": self.reset_reason,
            "buildSha256": self.build_sha256,
            "firmwareSha256": self.firmware_sha256,
            "configFilename": self.config_filename,
            "configSha256": self.config_sha256,
            "runtimeSha256": self.runtime_sha256,
            "backtraceSha256": self.backtrace_sha256,
            "journalSha256": self.journal_sha256,
            "stats": self.stats,
        }


def _resource_errors(stats: dict[str, Any]) -> list[str]:
    errors = []
    for field in RESOURCE_ZERO_FIELDS:
        if stats.get(field) != 0:
            errors.append(f"{field} must recover to 0, got {stats.get(field)!r}")
    for field, expected in RESOURCE_IN_BAND_FIELDS.items():
        if stats.get(field) != expected:
            errors.append(f"{field} expected in-band ESP420 owner {expected}, got {stats.get(field)!r}")
    return errors


def _lifecycle_gap(stats: dict[str, Any], created: str, destroyed: str) -> int | None:
    if not isinstance(stats.get(created), int) or not isinstance(stats.get(destroyed), int):
        return None
    return stats[created] - stats[destroyed]


def validate_preflight(snapshot: Snapshot, expected_hardening_id: str) -> list[str]:
    errors: list[str] = []
    missing = sorted(REQUIRED_STATS_FIELDS - snapshot.stats.keys())
    if missing:
        errors.append("required ESP420 telemetry missing: " + ", ".join(missing))
    if snapshot.state != "Idle":
        errors.append(f"machine must be Idle, got {snapshot.state!r}")
    if snapshot.hardening_id != expected_hardening_id:
        errors.append(
            f"diagnostic hardening ID mismatch: expected {expected_hardening_id!r}, got {snapshot.hardening_id!r}"
        )
    if not isinstance(snapshot.boot_sequence, int) or snapshot.boot_sequence <= 0:
        errors.append(f"invalid diagnostic boot sequence: {snapshot.boot_sequence!r}")
    if not isinstance(snapshot.uptime_ms, int) or snapshot.uptime_ms <= 0:
        errors.append(f"invalid diagnostic uptime: {snapshot.uptime_ms!r}")
    if not isinstance(snapshot.reset_reason, int):
        errors.append(f"invalid diagnostic reset reason: {snapshot.reset_reason!r}")
    errors.extend(_resource_errors(snapshot.stats))
    if snapshot.stats.get("Free memory", 0) < PREFLIGHT_IN_BAND_OBSERVER_MIN_BYTES:
        errors.append("preflight free heap is below the 28 KiB in-band ESP420 observer floor")
    if snapshot.stats.get("Largest free block", 0) < 20 * 1024:
        errors.append("preflight largest heap block is below the first-WebSocket 20 KiB admission reserve")
    return errors


def validate_continuity(before: Snapshot, current: Snapshot) -> list[str]:
    errors: list[str] = []
    if current.state != "Idle":
        errors.append(f"machine left Idle state: {current.state!r}")
    for attribute in IDENTITY_ATTRIBUTES:
        if getattr(current, attribute) != getattr(before, attribute):
            label = attribute.replace("_sha256", "").replace("_", " ")
            errors.append(f"{label} changed: {getattr(before, attribute)!r} -> {getattr(current, attribute)!r}")
    if current.reset_reason != before.reset_reason:
        errors.append(f"reset reason changed: {before.reset_reason!r} -> {current.reset_reason!r}")
    if not isinstance(current.uptime_ms, int) or not isinstance(before.uptime_ms, int) or current.uptime_ms < before.uptime_ms:
        errors.append(f"diagnostic uptime moved backwards: {before.uptime_ms!r} -> {current.uptime_ms!r}")
    for field in (
        "Web requests created",
        "Web requests destroyed",
        "Web request owners allocated",
        "Web request owners deallocated",
        "Async TCP clients created",
        "Async TCP clients destroyed",
        "Async TCP events created",
        "Async TCP events destroyed",
        "Async TCP accept event allocation failures",
        "Async TCP accept admission rejections",
    ):
        if isinstance(before.stats.get(field), int) and isinstance(current.stats.get(field), int):
            if current.stats[field] < before.stats[field]:
                errors.append(f"monotonic counter {field!r} decreased (probable reboot or telemetry reset)")
    return errors


def validate_postflight(before: Snapshot, after: Snapshot) -> list[str]:
    errors = validate_continuity(before, after)
    errors.extend(_resource_errors(after.stats))
    pairs = (
        ("Web requests created", "Web requests destroyed", "request lifecycle gap"),
        ("Web request owners allocated", "Web request owners deallocated", "request-owner lifecycle gap"),
        ("Async TCP clients created", "Async TCP clients destroyed", "TCP-client lifecycle gap"),
        ("Async TCP events created", "Async TCP events destroyed", "TCP-event lifecycle gap"),
    )
    for created, destroyed, label in pairs:
        initial = _lifecycle_gap(before.stats, created, destroyed)
        final = _lifecycle_gap(after.stats, created, destroyed)
        if initial is not None and final is not None and final != initial:
            errors.append(f"{label} did not recover exactly: baseline {initial}, final {final}")
    if after.stats.get("Free memory", 0) < 24 * 1024:
        errors.append("postflight free heap fell below 24 KiB")
    if after.stats.get("Largest free block", 0) < 12 * 1024:
        errors.append("postflight largest heap block fell below 12 KiB")
    if after.stats.get("Async TCP event queue depth", 0) > 4:
        errors.append("Async TCP event queue depth did not drain")
    if after.stats.get("TCP PCBs active", 0) > before.stats.get("TCP PCBs active", 0) + 2:
        errors.append("active TCP PCB count did not return near baseline")
    return errors


def validate_static_disconnect_attribution(
    disconnected: int, *, file_rejections: int, accept_rejections: int
) -> None:
    attributed = max(0, file_rejections) + max(0, accept_rejections)
    if disconnected > attributed:
        raise SoakAbort(
            f"{disconnected} static GET disconnects occurred but only "
            f"{file_rejections} file-stream and {accept_rejections} TCP-admission rejections were recorded"
        )


class FluidNCReadOnlyClient:
    def __init__(self, host: str, port: int = 80, timeout: float = 8.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    def get(self, path: str, *, allowed_statuses: Iterable[int] = (200,), limit: int = 2 * 1024 * 1024) -> tuple[int, bytes]:
        status, body = raw_http_get(self.host, self.port, path, self.timeout, limit)
        if status not in set(allowed_statuses):
            raise HttpStatusError(status, path)
        return status, body

    @staticmethod
    def command_path(command: str, *, esp: bool = False) -> str:
        parameter = "commandText" if esp else "plain"
        return "/command?" + parameter + "=" + urllib.parse.quote(command, safe="")

    def command(self, command: str, *, esp: bool = False) -> bytes:
        return self.get(self.command_path(command, esp=esp))[1]

    def _capture_state_and_stats(self) -> tuple[str, dict[str, Any]]:
        state_raw = self.command("$State")
        state_match = re.search(rb"State\s+\d+\s+\(([^)]+)\)", state_raw)
        if not state_match:
            raise SoakAbort(f"could not parse FluidNC state: {state_raw[:200]!r}")
        state = state_match.group(1).decode("ascii", "replace")
        return state, parse_esp420(self.command("[ESP420]json=yes", esp=True))

    def capture_runtime_snapshot(self, identity: Snapshot) -> Snapshot:
        """Fast guard sample without LocalFS/config reads that perturb heap pressure."""
        state, stats = self._capture_state_and_stats()
        return Snapshot(
            captured_utc=utc_now(),
            state=state,
            stats=stats,
            build_sha256=identity.build_sha256,
            firmware_sha256=identity.firmware_sha256,
            config_filename=identity.config_filename,
            config_sha256=identity.config_sha256,
            runtime_sha256=identity.runtime_sha256,
            backtrace_sha256=identity.backtrace_sha256,
            journal_sha256=identity.journal_sha256,
        )

    def capture_snapshot(self) -> Snapshot:
        state, stats = self._capture_state_and_stats()
        build_raw = self.command("$Build/Info")
        firmware_raw = self.command("[ESP800]json=yes", esp=True)
        filename_raw = self.command("$Config/Filename")
        filename_match = re.search(rb"(?m)^\$Config/Filename=([^\r\n]+)", filename_raw)
        if not filename_match:
            raise SoakAbort("could not parse $Config/Filename")
        config_filename = urllib.parse.unquote(filename_match.group(1).decode("utf-8").strip())
        webdav_path = "/flash/" + "/".join(urllib.parse.quote(part, safe="") for part in config_filename.lstrip("/").split("/"))
        config_raw = self.get(webdav_path)[1]
        runtime_raw = self.command("$Config/Dump")
        backtrace_raw = self.command("$Backtrace/Show")
        journal_status, journal_raw = self.get(
            "/flash/debug-crash-journal.jsonl", allowed_statuses=(200, 404)
        )
        journal_hash = sha256(journal_raw) if journal_status == 200 else "MISSING"
        return Snapshot(
            captured_utc=utc_now(),
            state=state,
            stats=stats,
            build_sha256=sha256(build_raw),
            firmware_sha256=sha256(firmware_raw),
            config_filename=config_filename,
            config_sha256=sha256(config_raw),
            runtime_sha256=sha256(runtime_raw),
            backtrace_sha256=sha256(backtrace_raw),
            journal_sha256=journal_hash,
        )


class RawWebSocket:
    def __init__(
        self,
        sock: socket.socket | None,
        accepted: bool,
        http_status: int,
        error: str | None = None,
        *,
        failure_kind: str | None = None,
        handshake_response_bytes: int = 0,
        handshake_elapsed_ms: int = 0,
    ):
        self.sock = sock
        self.accepted = accepted
        self.http_status = http_status
        self.error = error
        self.failure_kind = failure_kind
        self.handshake_response_bytes = handshake_response_bytes
        self.handshake_elapsed_ms = handshake_elapsed_ms
        self.application_frames_sent = 0
        self.control_frames_sent = 0
        self.ping_frames_received = 0
        self.last_ping_payload: bytes | None = None
        self._buffer = bytearray()
        self._send_lock = threading.Lock()
        self._stop = threading.Event()
        self.server_closed = threading.Event()
        self._reader: threading.Thread | None = None

    @classmethod
    def connect(
        cls,
        host: str,
        port: int = 80,
        *,
        timeout: float = 5.0,
        auto_pong: bool = True,
        path: str | None = None,
    ) -> "RawWebSocket":
        sock: socket.socket | None = None
        response = bytearray()
        started = time.monotonic()
        deadline = time.monotonic() + timeout
        try:
            sock = socket.create_connection((host, port), timeout=timeout)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("hard WebSocket connect deadline exceeded")
            sock.settimeout(remaining)
            key = base64.b64encode(os.urandom(16)).decode("ascii")
            nonce = os.urandom(6).hex()
            target = path or f"/?independent_session=1&codex_soak={nonce}"
            sock.sendall(build_websocket_handshake(host, port, target, key))
            while b"\r\n\r\n" not in response:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("hard WebSocket handshake deadline exceeded")
                sock.settimeout(remaining)
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response.extend(chunk)
                if len(response) > 32768:
                    raise SoakAbort("WebSocket handshake headers exceeded 32 KiB")
            header_end = response.find(b"\r\n\r\n")
            header_blob = bytes(response if header_end < 0 else response[:header_end])
            status_match = re.match(rb"HTTP/1\.[01]\s+([0-9]{3})", header_blob)
            status = int(status_match.group(1)) if status_match else 0
            if status != 101:
                sock.close()
                if not response:
                    failure_kind = "eof_before_http_status"
                    error = "peer closed before HTTP status"
                elif status == 0:
                    failure_kind = "invalid_http_status"
                    error = "invalid HTTP status in upgrade response"
                else:
                    failure_kind = "http_rejection"
                    error = "upgrade rejected"
                return cls(
                    None,
                    False,
                    status,
                    error,
                    failure_kind=failure_kind,
                    handshake_response_bytes=len(response),
                    handshake_elapsed_ms=int((time.monotonic() - started) * 1000),
                )
            headers: dict[str, str] = {}
            for line in header_blob.split(b"\r\n")[1:]:
                if b":" in line:
                    name, value = line.split(b":", 1)
                    headers[name.decode("ascii").lower()] = value.decode("ascii").strip()
            expected_accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode("ascii")).digest()).decode("ascii")
            if headers.get("sec-websocket-accept") != expected_accept:
                raise SoakAbort("invalid Sec-WebSocket-Accept in 101 response")
            client = cls(sock, True, status)
            if header_end >= 0:
                client._buffer.extend(response[header_end + 4 :])
            sock.settimeout(1.0)
            if auto_pong:
                client._reader = threading.Thread(target=client._reader_loop, daemon=True, name="fluidnc-soak-ws")
                client._reader.start()
            return client
        except Exception as error:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
            if isinstance(error, SoakAbort):
                raise
            failure_kind = (
                "peer_transport_abort"
                if isinstance(error, (ConnectionAbortedError, ConnectionResetError, BrokenPipeError))
                else "transport_timeout"
                if isinstance(error, TimeoutError)
                else "transport_exception"
            )
            return cls(
                None,
                False,
                0,
                f"{type(error).__name__}: {error}",
                failure_kind=failure_kind,
                handshake_response_bytes=len(response),
                handshake_elapsed_ms=int((time.monotonic() - started) * 1000),
            )

    def _recv_exact(self, size: int, *, retry_timeouts: bool = True, deadline: float | None = None) -> bytes:
        while len(self._buffer) < size:
            current_socket = self.sock
            if current_socket is None:
                raise EOFError
            if deadline is not None:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise socket.timeout("hard WebSocket frame deadline exceeded")
                current_socket.settimeout(min(1.0, remaining))
            try:
                chunk = current_socket.recv(max(4096, size - len(self._buffer)))
            except socket.timeout:
                if self._stop.is_set():
                    raise EOFError
                if not retry_timeouts:
                    raise
                continue
            if not chunk:
                raise EOFError
            self._buffer.extend(chunk)
        result = bytes(self._buffer[:size])
        del self._buffer[:size]
        return result

    def _recv_frame(self, *, retry_timeouts: bool = True, deadline: float | None = None) -> tuple[int, bytes]:
        first, second = self._recv_exact(2, retry_timeouts=retry_timeouts, deadline=deadline)
        opcode = first & 0x0F
        masked = bool(second & 0x80)
        length = second & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv_exact(2, retry_timeouts=retry_timeouts, deadline=deadline))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exact(8, retry_timeouts=retry_timeouts, deadline=deadline))[0]
        if length > 2 * 1024 * 1024:
            raise SoakAbort(f"WebSocket frame too large: {length}")
        mask = self._recv_exact(4, retry_timeouts=retry_timeouts, deadline=deadline) if masked else b""
        payload = self._recv_exact(length, retry_timeouts=retry_timeouts, deadline=deadline)
        if masked:
            payload = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        return opcode, payload

    def _send_control(self, opcode: int, payload: bytes = b"") -> None:
        if len(payload) > 125:
            return
        mask = os.urandom(4)
        header = bytes((0x80 | opcode, 0x80 | len(payload)))
        masked = bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
        with self._send_lock:
            current_socket = self.sock
            if current_socket is None:
                return
            current_socket.sendall(header + mask + masked)
        self.control_frames_sent += 1

    def _note_ping(self, payload: bytes) -> None:
        self.ping_frames_received += 1
        self.last_ping_payload = payload

    def _reader_loop(self) -> None:
        try:
            while not self._stop.is_set():
                opcode, payload = self._recv_frame()
                if opcode == 0x9:
                    self._note_ping(payload)
                    self._send_control(0xA, payload)
                elif opcode == 0x8:
                    try:
                        self._send_control(0x8, payload[:125])
                    except OSError:
                        pass
                    self.server_closed.set()
                    return
        except (EOFError, OSError, SoakAbort):
            self.server_closed.set()

    def wait_for_server_close(self, timeout: float) -> bool:
        if self.server_closed.is_set():
            return True
        deadline = time.monotonic() + timeout
        if self.sock is None:
            return True
        self.sock.settimeout(min(1.0, timeout))
        try:
            while time.monotonic() < deadline:
                try:
                    opcode, payload = self._recv_frame(retry_timeouts=False, deadline=deadline)
                    if opcode == 0x9:
                        self._note_ping(payload)
                    elif opcode == 0x8:
                        try:
                            self._send_control(0x8, payload[:125])
                        except OSError:
                            pass
                        self.server_closed.set()
                        return True
                except socket.timeout:
                    continue
                except EOFError:
                    self.server_closed.set()
                    return True
        except OSError:
            self.server_closed.set()
            return True
        return False

    def close(self, *, graceful: bool = True) -> None:
        if self.sock is None:
            return
        if graceful and self.accepted and not self.server_closed.is_set():
            try:
                self._send_control(0x8, struct.pack("!H", 1000))
            except OSError:
                pass
            if self._reader is not None and self._reader is not threading.current_thread():
                self.server_closed.wait(2)
        self._stop.set()
        with self._send_lock:
            current_socket, self.sock = self.sock, None
        if current_socket is not None:
            try:
                current_socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                current_socket.close()
            except OSError:
                pass
        if self._reader is not None and self._reader is not threading.current_thread():
            self._reader.join(2)


def abort_websocket_upgrade(host: str, port: int, timeout: float) -> dict[str, Any]:
    """Send a complete upgrade request, then RST before reading the response."""
    started = time.monotonic()
    deadline = started + timeout
    sock: socket.socket | None = None
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("hard aborted-upgrade connect deadline exceeded")
        sock.settimeout(remaining)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        target = f"/?independent_session=1&codex_abort={os.urandom(6).hex()}"
        sock.sendall(build_websocket_handshake(host, port, target, key))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("hh", 1, 0))
        return {"sent": True, "elapsedMs": round((time.monotonic() - started) * 1000), "error": None}
    except OSError as error:
        return {"sent": False, "elapsedMs": round((time.monotonic() - started) * 1000), "error": str(error)}
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


def abort_tcp_before_http(host: str, port: int, timeout: float) -> dict[str, Any]:
    """Complete TCP connect, then send an immediate RST without HTTP bytes."""
    started = time.monotonic()
    sock: socket.socket | None = None
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("hh", 1, 0))
        return {"connected": True, "elapsedMs": round((time.monotonic() - started) * 1000), "error": None}
    except OSError as error:
        return {"connected": False, "elapsedMs": round((time.monotonic() - started) * 1000), "error": str(error)}
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


def open_partial_http(host: str, port: int, timeout: float) -> tuple[socket.socket | None, str | None]:
    """Open one bounded incomplete HTTP request; caller owns and closes it."""
    started = time.monotonic()
    sock: socket.socket | None = None
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
        remaining = timeout - (time.monotonic() - started)
        if remaining <= 0:
            raise TimeoutError("hard partial-HTTP connect deadline exceeded")
        sock.settimeout(remaining)
        sock.sendall(
            (
                f"GET / HTTP/1.1\r\nHost: {host}:{port}\r\n"
                "User-Agent: FluidNC-read-only-soak/1\r\n"
            ).encode("ascii")
        )
        return sock, None
    except OSError as error:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
        return None, str(error)


def partial_http_socket_pending(sock: socket.socket) -> bool:
    """Return true only while an incomplete request is still silent and open.

    This check is deliberately local to the already-open transport.  Opening an
    in-band telemetry request while the full server cap is held would create a
    ninth connection and correctly trigger the firmware's early admission
    rejection instead of measuring the eight partial requests under test.
    """
    try:
        readable, _writable, _exceptional = select.select([sock], [], [sock], 0)
        return not readable and not _exceptional
    except (OSError, ValueError):
        return False


def probe_partial_http_hard_cap(host: str, port: int, timeout: float) -> dict[str, Any]:
    """Attempt one read-only request above the eight-transport hard cap.

    A transport error is only provisional evidence.  The stage later requires
    exactly one matching firmware admission-counter increment after the held
    sockets are closed and telemetry is reachable again.
    """
    path = "/command?plain=%24State"
    try:
        status, body = raw_http_get(host, port, path, timeout, 4096)
        return {
            "rejected": False,
            "httpStatus": status,
            "responseBytes": len(body),
            "error": None,
        }
    except SoakAbort as error:
        return {
            "rejected": True,
            "httpStatus": 0,
            "responseBytes": 0,
            "error": str(error),
        }


def connect_static_socket(
    host: str, port: int, timeout: float, receive_buffer_bytes: int
) -> tuple[socket.socket, int]:
    """Connect a static-transfer socket with backpressure configured first."""
    if receive_buffer_bytes <= 0:
        sock = socket.create_connection((host, port), timeout=timeout)
        return sock, sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)

    last_error: OSError | None = None
    for family, socktype, proto, _canonname, address in socket.getaddrinfo(
        host, port, type=socket.SOCK_STREAM
    ):
        sock = socket.socket(family, socktype, proto)
        try:
            # This must happen before connect so the initial advertised receive
            # window cannot absorb the complete 120-KiB index asset.
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer_bytes)
            sock.settimeout(timeout)
            sock.connect(address)
            actual = sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
            return sock, actual
        except OSError as error:
            last_error = error
            sock.close()
    if last_error is not None:
        raise last_error
    raise OSError(f"no TCP address resolved for {host}:{port}")


def _http_response_shape(payload: bytes) -> tuple[int, int, int | None, bool]:
    match = re.match(rb"HTTP/1\.[01]\s+([0-9]{3})", payload)
    header_end = payload.find(b"\r\n\r\n")
    if match is None or header_end < 0:
        return 0, 0, None, False

    status = int(match.group(1))
    header_lines = payload[:header_end].split(b"\r\n")[1:]
    headers: dict[bytes, bytes] = {}
    for line in header_lines:
        name, separator, value = line.partition(b":")
        if separator:
            headers[name.strip().lower()] = value.strip()
    body = payload[header_end + 4 :]

    declared: int | None = None
    content_length = headers.get(b"content-length")
    if content_length is not None:
        try:
            declared = int(content_length)
        except ValueError:
            return status, len(body), None, False
        return status, len(body), declared, len(body) == declared

    if b"chunked" in headers.get(b"transfer-encoding", b"").lower():
        cursor = 0
        while True:
            line_end = body.find(b"\r\n", cursor)
            if line_end < 0:
                return status, len(body), None, False
            size_text = body[cursor:line_end].split(b";", 1)[0].strip()
            try:
                chunk_size = int(size_text, 16)
            except ValueError:
                return status, len(body), None, False
            cursor = line_end + 2
            if chunk_size == 0:
                trailer = body[cursor:]
                return status, len(body), None, trailer == b"\r\n" or trailer.endswith(b"\r\n\r\n")
            chunk_end = cursor + chunk_size
            if chunk_end + 2 > len(body) or body[chunk_end : chunk_end + 2] != b"\r\n":
                return status, len(body), None, False
            cursor = chunk_end + 2

    # A close-delimited GET response is complete because this helper only
    # evaluates payload after observing orderly EOF.
    return status, len(body), None, True


def slow_static_get(
    host: str,
    port: int,
    path: str,
    timeout: float,
    read_delay: float,
    *,
    first_chunk_event: threading.Event | None = None,
    first_chunk_release_event: threading.Event | None = None,
    receive_buffer_bytes: int = 0,
) -> dict[str, Any]:
    started = time.monotonic()
    deadline = started + timeout
    sock: socket.socket | None = None
    actual_receive_buffer = 0
    try:
        sock, actual_receive_buffer = connect_static_socket(host, port, timeout, receive_buffer_bytes)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise SoakAbort(f"hard static GET connect deadline exceeded for {path}")
        sock.settimeout(remaining)
        request = (
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nConnection: close\r\n"
            "Accept-Encoding: gzip\r\nUser-Agent: FluidNC-read-only-soak/1\r\n\r\n"
        ).encode("ascii")
        sock.sendall(request)
        payload = bytearray()
        first_chunk = True
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SoakAbort(f"hard static GET deadline exceeded for {path}")
            sock.settimeout(remaining)
            if first_chunk and first_chunk_event is not None:
                peeked = sock.recv(1, socket.MSG_PEEK)
                if not peeked:
                    break
                first_chunk_event.set()
                if first_chunk_release_event is not None:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0 or not first_chunk_release_event.wait(remaining):
                        raise SoakAbort(f"hard static GET lifecycle-release deadline exceeded for {path}")
            chunk = sock.recv(512)
            if not chunk:
                break
            payload.extend(chunk)
            if first_chunk:
                first_chunk = False
                if first_chunk_event is not None and not first_chunk_event.is_set():
                    first_chunk_event.set()
            if len(payload) > 2 * 1024 * 1024:
                raise SoakAbort("static GET response exceeded 2 MiB")
            if read_delay:
                time.sleep(read_delay)
        status, body_bytes, declared_body_bytes, complete = _http_response_shape(bytes(payload))
        return {
            "path": path,
            "status": status,
            "bytes": len(payload),
            "bodyBytes": body_bytes,
            "declaredBodyBytes": declared_body_bytes,
            "complete": complete,
            "receiveBufferBytes": actual_receive_buffer,
            "elapsedMs": round((time.monotonic() - started) * 1000),
            "error": None,
        }
    except (OSError, SoakAbort) as error:
        return {
            "path": path,
            "status": 0,
            "bytes": 0,
            "bodyBytes": 0,
            "declaredBodyBytes": None,
            "complete": False,
            "receiveBufferBytes": actual_receive_buffer,
            "elapsedMs": round((time.monotonic() - started) * 1000),
            "error": str(error),
        }
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass


class EvidenceRecorder:
    def __init__(self, output: Path, arguments: dict[str, Any]):
        self.output = output
        self.output.mkdir(parents=True, exist_ok=False)
        self.events_path = self.output / "events.jsonl"
        self.report_path = self.output / "soak-report.json"
        self.report: dict[str, Any] = {
            "schema": 1,
            "startedUtc": utc_now(),
            "status": "RUNNING",
            "safetyContract": {
                "allowed": ["HTTP GET", "RFC6455 handshake", "WebSocket PONG/CLOSE control frame", "TCP close"],
                "prohibited": [
                    "WebSocket application data",
                    "G-code/motion/homing/jog/macro",
                    "configuration/preferences write",
                    "firmware upload",
                    "reset or power-cycle",
                ],
            },
            "arguments": arguments,
            "stages": [],
        }
        self._lock = threading.Lock()

    def event(self, kind: str, **payload: Any) -> None:
        record = {"utc": utc_now(), "kind": kind, **payload}
        line = json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"
        with self._lock:
            with self.events_path.open("a", encoding="utf-8", newline="\n") as stream:
                stream.write(line)

    def finish(self, status: str, **payload: Any) -> None:
        self.report.update(payload)
        self.report["status"] = status
        self.report["finishedUtc"] = utc_now()
        temporary = self.report_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(self.report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        os.replace(temporary, self.report_path)


PROFILE = {
    # A bounded, read-only reproducer for transport-level parallel-upgrade
    # failures.  It deliberately avoids the long churn/static/stale stages so
    # every recorded HTTP000 can be attributed to the handshake burst itself.
    "parallel-repro": {
        "parallelCycles": 100,
    },
    "smoke": {
        "parallelCycles": 3,
        "churnCycles": 20,
        "earlyRstRounds": 4,
        "partialRounds": 1,
        "staticRounds": 3,
        "staleClients": 1,
    },
    "soak": {
        "parallelCycles": 100,
        "churnCycles": 500,
        "earlyRstRounds": 50,
        "partialRounds": 25,
        "staticRounds": 50,
        "staleClients": 2,
    },
}

# ESP-IDF/LwIP retains closed TCP PCBs for roughly two maximum-segment
# lifetimes.  Polling ESP420 while waiting is counterproductive because every
# command response is itself `Connection: close` and creates another TIME_WAIT
# PCB.  A recovery probe therefore has one deliberately silent window before
# exactly one new WebSocket handshake.
CHURN_BACKPRESSURE_QUIET_SECONDS = 125.0
SAME_SESSION_REGISTRATION_SECONDS = 0.5
FIRST_SOCKET_RECOVERY_REARM_SECONDS = 65.0
IN_BAND_CLIENT_RELEASE_SECONDS = 0.5
# A patched heap-admission rejection closes the transport directly instead of
# allocating an HTTP status response.  The peer must observe a bounded abort;
# a client-side timeout is still a diagnostic failure, never an accepted reject.
# An eight-way board measurement showed the last queued, already-counted abort
# at 1.077s, so retain a narrow 1.5s ceiling rather than treating it as a
# timeout or widening this all the way to the client handshake deadline.
FAST_POLICY_TRANSPORT_REJECT_MAX_MS = 1500


def is_fast_policy_transport_rejection(client: "RawWebSocket", timeout_seconds: float) -> bool:
    return (
        client.http_status == 0
        and client.failure_kind in {"eof_before_http_status", "peer_transport_abort"}
        and client.handshake_response_bytes == 0
        and 0 <= client.handshake_elapsed_ms <= min(FAST_POLICY_TRANSPORT_REJECT_MAX_MS, int(timeout_seconds * 1000))
    )


class SoakHarness:
    def __init__(self, args: argparse.Namespace, recorder: EvidenceRecorder):
        self.args = args
        self.recorder = recorder
        self.http = FluidNCReadOnlyClient(args.host, args.port, args.http_timeout)
        self.baseline: Snapshot | None = None
        self.last_snapshot: Snapshot | None = None
        self.capacity = 0
        self.open_clients: list[RawWebSocket] = []

    def _capture_guard(self, label: str) -> Snapshot:
        if self.baseline is None:
            raise SoakAbort("internal error: runtime guard requested before baseline")
        snapshot = self.http.capture_runtime_snapshot(self.baseline)
        errors = validate_continuity(self.baseline, snapshot)
        if errors:
            raise SoakAbort(f"{label}: " + "; ".join(errors))
        self.recorder.event("snapshot", label=label, snapshot=snapshot.to_dict())
        self.last_snapshot = snapshot
        return snapshot

    def _capture_full_guard(self, label: str) -> Snapshot:
        if self.baseline is None:
            raise SoakAbort("internal error: full guard requested before baseline")
        snapshot = self.http.capture_snapshot()
        errors = validate_continuity(self.baseline, snapshot) + _resource_errors(snapshot.stats)
        if errors:
            raise SoakAbort(f"{label}: " + "; ".join(errors))
        self.recorder.event("identity-checkpoint", label=label, snapshot=snapshot.to_dict())
        self.last_snapshot = snapshot
        return snapshot

    def _capture_light_state(self, label: str) -> str:
        state_raw = self.http.command("$State")
        state_match = re.search(rb"State\s+\d+\s+\(([^)]+)\)", state_raw)
        if not state_match:
            raise SoakAbort(f"{label}: could not parse FluidNC state: {state_raw[:200]!r}")
        state = state_match.group(1).decode("ascii", "replace")
        self.recorder.event("light-state", label=label, state=state)
        return state

    def _close_all(self) -> None:
        clients, self.open_clients = self.open_clients, []
        for index, client in enumerate(clients):
            client.close(graceful=(index % 5 != 4))

    def _wait_recovered(self, label: str, timeout: float = 20.0) -> Snapshot:
        deadline = time.monotonic() + timeout
        last_errors: list[str] = []
        while time.monotonic() < deadline:
            try:
                snapshot = self.http.capture_runtime_snapshot(self.baseline)
            except HttpStatusError as error:
                if error.status != 503:
                    raise
                last_errors = [f"Heavy ESP420 recovery observer remained HTTP {error.status}"]
                self.recorder.event(
                    "recovery-observer-rejected",
                    label=label,
                    httpStatus=error.status,
                )
                time.sleep(1)
                continue
            if self.baseline is None:
                return snapshot
            last_errors = validate_continuity(self.baseline, snapshot) + _resource_errors(snapshot.stats)
            if not last_errors:
                self.recorder.event("recovered", label=label, snapshot=snapshot.to_dict())
                self.last_snapshot = snapshot
                return snapshot
            time.sleep(1)
        raise SoakAbort(f"{label} did not recover within {timeout}s: {'; '.join(last_errors)}")

    def _wait_postflight_stable(self, identity: Snapshot, timeout: float = 30.0) -> Snapshot:
        if self.baseline is None:
            raise SoakAbort("internal error: postflight stability requested before baseline")
        deadline = time.monotonic() + timeout
        consecutive = 0
        last_errors: list[str] = []
        last_snapshot = identity
        while time.monotonic() < deadline:
            last_snapshot = self.http.capture_runtime_snapshot(identity)
            last_errors = validate_postflight(self.baseline, last_snapshot)
            self.recorder.event(
                "postflight-stability-sample",
                consecutivePasses=consecutive,
                errors=last_errors,
                snapshot=last_snapshot.to_dict(),
            )
            if last_errors:
                consecutive = 0
            else:
                consecutive += 1
                if consecutive == 3:
                    return last_snapshot
            time.sleep(1)
        raise SoakAbort(f"postflight did not produce three stable quiescent samples: {'; '.join(last_errors)}")

    def _quiet_transport_recovery(self, label: str) -> dict[str, Any]:
        quiet_seconds = self.args.inter_stage_quiet_seconds
        rearm_seconds = self.args.post_recovery_quiet_seconds
        result = {
            "label": label,
            "quietSeconds": quiet_seconds,
            "rearmSeconds": rearm_seconds,
            "reconnected": False,
        }
        if quiet_seconds <= 0:
            return result
        self.recorder.event("inter-stage-quiet", label=label, quietSeconds=quiet_seconds)
        time.sleep(quiet_seconds)
        probe = RawWebSocket.connect(
            self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True
        )
        if not probe.accepted:
            probe.close(graceful=False)
            snapshot = self._capture_guard(label + "-probe-failed")
            raise SoakAbort(
                f"{label} did not regain WebSocket admission after {quiet_seconds}s without polling "
                f"(HTTP {probe.http_status}, {probe.error}; heap rejects "
                f"{snapshot.stats.get('Web WS heap rejections')}, limit rejects "
                f"{snapshot.stats.get('Web WS limit rejections')})"
            )
        probe.close(graceful=False)
        result["reconnected"] = True
        if rearm_seconds > 0:
            self.recorder.event("inter-stage-rearm", label=label, quietSeconds=rearm_seconds)
            time.sleep(rearm_seconds)
        self.recorder.event("inter-stage-recovered", **result)
        return result

    def _stage_ramp(self) -> dict[str, Any]:
        attempts = []
        previous_stats = self._capture_guard("ramp-before").stats
        # The guard above is a Heavy ESP420 response and therefore still owns
        # its reserved HTTP slot while its TCP disconnect is being dispatched.
        # Do not contaminate the first WebSocket admission with that observer.
        time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)
        for index in range(1, self.args.max_connections + 1):
            client = RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True)
            if client.accepted:
                self.open_clients.append(client)
                self.capacity = len(self.open_clients)
            time.sleep(0.15)
            snapshot = self._capture_guard(f"ramp-{index}")
            attempts.append(
                {
                    "attempt": index,
                    "accepted": client.accepted,
                    "httpStatus": client.http_status,
                    "error": client.error,
                    "active": snapshot.stats.get("Web WS active"),
                    "pending": snapshot.stats.get("Web WS pending"),
                    "heapRejections": snapshot.stats.get("Web WS heap rejections"),
                    "limitRejections": snapshot.stats.get("Web WS limit rejections"),
                    "freeBytes": snapshot.stats.get("Free memory"),
                    "largestBlockBytes": snapshot.stats.get("Largest free block"),
                }
            )
            if snapshot.stats.get("Web WS active") != len(self.open_clients):
                raise SoakAbort(
                    f"ramp accounting mismatch: host holds {len(self.open_clients)}, telemetry reports "
                    f"{snapshot.stats.get('Web WS active')}"
                )
            if not client.accepted:
                rejected_before = previous_stats.get("Web WS heap rejections", 0) + previous_stats.get("Web WS limit rejections", 0)
                rejected_after = snapshot.stats.get("Web WS heap rejections", 0) + snapshot.stats.get("Web WS limit rejections", 0)
                if rejected_after <= rejected_before:
                    raise SoakAbort("WebSocket refusal was not attributed to a firmware admission counter")
            previous_stats = snapshot.stats
        if self.capacity < 1 or self.capacity > self.args.max_connections:
            raise SoakAbort(f"invalid discovered WebSocket capacity: {self.capacity}")
        self._close_all()
        self._wait_recovered("ramp-cleanup")
        return {"name": "sequential-ramp", "capacity": self.capacity, "attempts": attempts}

    def _parallel_connect(self, count: int) -> list[RawWebSocket]:
        start = threading.Event()

        def connect() -> RawWebSocket:
            start.wait()
            return RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True)

        with concurrent.futures.ThreadPoolExecutor(max_workers=count) as pool:
            futures = [pool.submit(connect) for _ in range(count)]
            start.set()
            clients: list[RawWebSocket] = []
            try:
                for future in concurrent.futures.as_completed(futures):
                    clients.append(future.result())
                return clients
            except Exception:
                for future in futures:
                    if not future.done():
                        future.cancel()
                concurrent.futures.wait(futures)
                for future in futures:
                    try:
                        client = future.result()
                    except Exception:
                        continue
                    if client not in clients:
                        clients.append(client)
                for client in clients:
                    client.close(graceful=False)
                raise

    def _stage_parallel(self, cycles: int) -> dict[str, Any]:
        summaries = []
        previous = self._capture_guard("parallel-before")
        for cycle in range(1, cycles + 1):
            clients = self._parallel_connect(self.args.max_connections)
            self.open_clients.extend(client for client in clients if client.accepted)
            time.sleep(0.15)
            snapshot = self._capture_guard(f"parallel-{cycle}")
            if snapshot.stats.get("Web WS active") != len(self.open_clients):
                raise SoakAbort("parallel handshake accounting mismatch")
            rejected_clients = [client for client in clients if not client.accepted]
            rejected = len(rejected_clients)
            before_rejections = previous.stats.get("Web WS heap rejections", 0) + previous.stats.get("Web WS limit rejections", 0)
            after_rejections = snapshot.stats.get("Web WS heap rejections", 0) + snapshot.stats.get("Web WS limit rejections", 0)
            rejection_delta = after_rejections - before_rejections
            zero_status_clients = [client for client in rejected_clients if client.http_status == 0]
            if zero_status_clients and not (
                rejection_delta == rejected
                and all(is_fast_policy_transport_rejection(client, self.args.ws_timeout) for client in zero_status_clients)
            ):
                failures = [
                    {
                        "status": client.http_status,
                        "kind": client.failure_kind,
                        "error": client.error,
                        "responseBytes": client.handshake_response_bytes,
                        "elapsedMs": client.handshake_elapsed_ms,
                    }
                    for client in zero_status_clients
                ]
                self.recorder.event(
                    "parallel-http000",
                    cycle=cycle,
                    failures=failures,
                    rejectionDelta=rejection_delta,
                    rxTimeoutDelta=snapshot.stats.get("Async TCP RX timeouts", 0)
                    - previous.stats.get("Async TCP RX timeouts", 0),
                    acceptAdmissionRejectionDelta=snapshot.stats.get("Async TCP accept admission rejections", 0)
                    - previous.stats.get("Async TCP accept admission rejections", 0),
                )
                raise SoakAbort("parallel handshake produced an unattributed HTTP000/transport failure")
            if zero_status_clients:
                self.recorder.event(
                    "parallel-attributed-transport-rejection",
                    cycle=cycle,
                    failures=[
                        {
                            "status": client.http_status,
                            "kind": client.failure_kind,
                            "error": client.error,
                            "responseBytes": client.handshake_response_bytes,
                            "elapsedMs": client.handshake_elapsed_ms,
                        }
                        for client in zero_status_clients
                    ],
                    rejectionDelta=rejection_delta,
                )
            rejected = len(rejected_clients)
            before_rejections = previous.stats.get("Web WS heap rejections", 0) + previous.stats.get("Web WS limit rejections", 0)
            after_rejections = snapshot.stats.get("Web WS heap rejections", 0) + snapshot.stats.get("Web WS limit rejections", 0)
            if after_rejections - before_rejections < rejected:
                raise SoakAbort(
                    f"parallel cycle {cycle} had {rejected} rejected handshakes but only "
                    f"{after_rejections - before_rejections} firmware rejection-counter increments"
                )
            if cycle == 1 or cycle % 10 == 0 or cycle == cycles:
                summaries.append(
                    {
                        "cycle": cycle,
                        "accepted": len(self.open_clients),
                        "rejected": rejected,
                        "stats": snapshot.stats,
                    }
                )
            self._close_all()
            previous = self._wait_recovered(f"parallel-cleanup-{cycle}")
        return {"name": "parallel-handshakes", "cycles": cycles, "samples": summaries}

    def _stage_churn(self, cycles: int) -> dict[str, Any]:
        accepted = rejected = 0
        backpressure_events = []
        if self.baseline is None:
            raise SoakAbort("internal error: churn requested before baseline")
        previous_heap_rejections = self.baseline.stats.get("Web WS heap rejections", 0)
        previous_limit_rejections = self.baseline.stats.get("Web WS limit rejections", 0)
        for cycle in range(1, cycles + 1):
            client = RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True)
            if client.accepted:
                accepted += 1
            else:
                rejected += 1
                client.close(graceful=False)
                snapshot = self._capture_guard(f"churn-backpressure-{cycle}")
                heap_rejections = snapshot.stats.get("Web WS heap rejections", 0)
                limit_rejections = snapshot.stats.get("Web WS limit rejections", 0)
                if heap_rejections <= previous_heap_rejections or limit_rejections != previous_limit_rejections:
                    raise SoakAbort(
                        f"clean single-client churn cycle {cycle} refusal was not attributed exclusively to the "
                        f"heap admission guard (heap {previous_heap_rejections}->{heap_rejections}, "
                        f"limit {previous_limit_rejections}->{limit_rejections})"
                    )
                resource_errors = _resource_errors(snapshot.stats)
                if resource_errors:
                    raise SoakAbort(
                        f"clean single-client churn cycle {cycle} left tracked resources after refusal: "
                        + "; ".join(resource_errors)
                    )

                self.recorder.event(
                    "churn-backpressure-quiet",
                    cycle=cycle,
                    httpStatus=client.http_status,
                    quietSeconds=CHURN_BACKPRESSURE_QUIET_SECONDS,
                    snapshot=snapshot.to_dict(),
                )
                time.sleep(CHURN_BACKPRESSURE_QUIET_SECONDS)
                recovery = RawWebSocket.connect(
                    self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True
                )
                if not recovery.accepted:
                    recovery.close(graceful=False)
                    raise SoakAbort(
                        f"clean single-client churn cycle {cycle} did not self-recover after "
                        f"{CHURN_BACKPRESSURE_QUIET_SECONDS}s quiet (HTTP {recovery.http_status}, {recovery.error})"
                    )
                accepted += 1
                recovery.close(graceful=False)
                event = {
                    "cycle": cycle,
                    "httpStatus": client.http_status,
                    "quietSeconds": CHURN_BACKPRESSURE_QUIET_SECONDS,
                    "recovered": True,
                }
                backpressure_events.append(event)
                self.recorder.event("churn-backpressure-recovered", **event)
                previous_heap_rejections = heap_rejections
                previous_limit_rejections = limit_rejections
                continue
            client.close(graceful=(cycle % 5 != 0))
            if cycle % 25 == 0 or cycle == cycles:
                self._wait_recovered(f"churn-{cycle}")
        return {
            "name": "connect-close-churn",
            "cycles": cycles,
            "accepted": accepted,
            "rejected": rejected,
            "backpressureEvents": backpressure_events,
        }

    def _stage_aborted_upgrades(self) -> dict[str, Any]:
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.args.max_connections) as pool:
            results = list(
                pool.map(
                    lambda _index: abort_websocket_upgrade(self.args.host, self.args.port, self.args.ws_timeout),
                    range(self.args.max_connections),
                )
            )
        sent = sum(result["sent"] for result in results)
        if sent != self.args.max_connections:
            errors = [result["error"] for result in results if result["error"]]
            raise SoakAbort(
                f"aborted-upgrade burst sent only {sent}/{self.args.max_connections} complete requests: {errors}"
            )
        reachable_state = self._capture_light_state("aborted-upgrades-reachable")
        if reachable_state != "Idle":
            raise SoakAbort(f"aborted-upgrade burst left machine state {reachable_state!r}")
        # Do not start the Heavy ESP420 observer while AsyncTCP is still
        # reaping the intentionally reset upgrade requests.  The light state
        # guard above proves reachability without competing for that slot.
        time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)
        recovered = self._wait_recovered("aborted-upgrades-cleanup", timeout=15)
        return {
            "name": "aborted-upgrade-recovery",
            "attempts": len(results),
            "sent": sent,
            "errors": [result["error"] for result in results if result["error"]],
            "reachableState": reachable_state,
            "recoveredPending": recovered.stats.get("Web WS pending"),
        }

    def _stage_early_rst(self, rounds: int) -> dict[str, Any]:
        before = self._capture_guard("early-rst-before")
        results = []
        for round_number in range(1, rounds + 1):
            with concurrent.futures.ThreadPoolExecutor(max_workers=self.args.max_connections) as pool:
                burst = list(
                    pool.map(
                        lambda _index: abort_tcp_before_http(self.args.host, self.args.port, self.args.ws_timeout),
                        range(self.args.max_connections),
                    )
                )
            connected = sum(result["connected"] for result in burst)
            results.extend(burst)
            if connected != self.args.max_connections:
                errors = [result["error"] for result in burst if result["error"]]
                raise SoakAbort(
                    f"early-RST burst {round_number} connected only {connected}/{self.args.max_connections}: {errors}"
                )
        recovered = self._wait_recovered("early-rst-cleanup", timeout=15)
        before_gap = before.stats["Async TCP clients created"] - before.stats["Async TCP clients destroyed"]
        after_gap = recovered.stats["Async TCP clients created"] - recovered.stats["Async TCP clients destroyed"]
        cleanup_delta = recovered.stats["Async TCP early RST accept cleanups"] - before.stats[
            "Async TCP early RST accept cleanups"
        ]
        if cleanup_delta <= 0:
            raise SoakAbort("early-RST pressure did not exercise the unpublished ACCEPT cleanup path")
        if after_gap != before_gap:
            raise SoakAbort(f"early-RST client lifecycle gap grew from {before_gap} to {after_gap}")
        return {
            "name": "tcp-rst-before-dispatch",
            "rounds": rounds,
            "attempts": len(results),
            "connected": sum(result["connected"] for result in results),
            "clientGapBefore": before_gap,
            "clientGapAfter": after_gap,
            "earlyRstCleanupDelta": cleanup_delta,
            "pendingAfter": recovered.stats.get("Async TCP server pending accepts"),
            "acceptAdmissionRejectionDelta": recovered.stats.get("Async TCP accept admission rejections", 0)
            - before.stats.get("Async TCP accept admission rejections", 0),
        }

    @staticmethod
    def _close_partial_sockets(sockets: list[socket.socket]) -> None:
        for sock in sockets:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                sock.close()
            except OSError:
                pass

    def _stage_partial_http(self, rounds: int) -> dict[str, Any]:
        samples = []
        opened_total = 0
        previous = self.last_snapshot
        if previous is None:
            raise SoakAbort("partial HTTP stage has no prior poll-free counter checkpoint")
        for round_number in range(1, rounds + 1):
            with concurrent.futures.ThreadPoolExecutor(max_workers=self.args.max_connections) as pool:
                opened = list(
                    pool.map(
                        lambda _index: open_partial_http(self.args.host, self.args.port, self.args.http_timeout),
                        range(self.args.max_connections),
                    )
                )
            sockets = [sock for sock, _error in opened if sock is not None]
            errors = [error for _sock, error in opened if error]
            if len(sockets) != self.args.max_connections:
                self._close_partial_sockets(sockets)
                raise SoakAbort(
                    f"partial HTTP round {round_number} opened only {len(sockets)}/{self.args.max_connections}: {errors}"
                )
            opened_total += len(sockets)
            held_pending = 0
            cap_probe = None
            try:
                time.sleep(self.args.partial_hold_seconds)
                held_pending = sum(partial_http_socket_pending(sock) for sock in sockets)
                if held_pending == self.args.max_connections and self.args.max_connections == ASYNC_TCP_HARD_CAP:
                    cap_probe = probe_partial_http_hard_cap(
                        self.args.host, self.args.port, self.args.http_timeout
                    )
            finally:
                self._close_partial_sockets(sockets)
            if held_pending != self.args.max_connections:
                raise SoakAbort(
                    f"partial HTTP round {round_number} kept only {held_pending}/{self.args.max_connections} "
                    "requests pending for the full hold interval"
                )
            if cap_probe is not None and not cap_probe["rejected"]:
                raise SoakAbort(
                    f"partial HTTP round {round_number} unexpectedly admitted a ninth transport "
                    f"(HTTP {cap_probe['httpStatus']}, {cap_probe['responseBytes']} bytes)"
                )
            snapshot = self._capture_guard(f"partial-http-{round_number}")
            rejection_delta = snapshot.stats.get("Async TCP accept admission rejections", 0) - previous.stats.get(
                "Async TCP accept admission rejections", 0
            )
            expected_rejections = 1 if cap_probe is not None else 0
            if rejection_delta != expected_rejections:
                raise SoakAbort(
                    f"partial HTTP round {round_number} expected {expected_rejections} exactly attributed "
                    f"TCP admission rejection(s), observed {rejection_delta}"
                )
            samples.append(
                {
                    "round": round_number,
                    "opened": len(sockets),
                    "heldPending": held_pending,
                    "connectErrors": errors,
                    "postCloseTcpActive": snapshot.stats.get("TCP PCBs active"),
                    "postCloseFreeBytes": snapshot.stats.get("Free memory"),
                    "postCloseLargestBlockBytes": snapshot.stats.get("Largest free block"),
                    "acceptAdmissionRejectionDelta": rejection_delta,
                    "hardCapProbe": cap_probe,
                }
            )
            previous = snapshot
            if round_number % 10 == 0 or round_number == rounds:
                self._wait_recovered(f"partial-http-cleanup-{round_number}")
            if round_number < rounds:
                # The most recent ESP420 response (including the recovery
                # sample above at batch boundaries) owns an AsyncTCP client
                # until its disconnect event is dispatched.  Leave a poll-free
                # grace interval before occupying all eight slots again.
                time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)
        return {"name": "partial-http-recovery", "rounds": rounds, "openedTotal": opened_total, "samples": samples}

    def _stage_same_session(self) -> dict[str, Any]:
        if self.capacity < 2:
            return {
                "name": "same-session-newest-wins",
                "skipped": True,
                "reason": "dynamic independent-session capacity is below two; stale eviction remains the replacement path",
            }
        first = RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True, path="/")
        if not first.accepted:
            raise SoakAbort("same-session stage could not admit the first client")
        self.open_clients.append(first)
        # A 101 response precedes the asynchronous WS_EVT_CONNECT callback that
        # installs the browser-session channel.  Do not race the replacement
        # handshake against that registration, and do not poll ESP420 here (the
        # command response would create another TIME_WAIT PCB).
        time.sleep(SAME_SESSION_REGISTRATION_SECONDS)
        second = RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True, path="/")
        if not second.accepted:
            raise SoakAbort("same-session stage could not admit the replacement client")
        self.open_clients.append(second)
        if not first.server_closed.wait(12):
            raise SoakAbort("newest-wins did not close the older same-session WebSocket within 12s")
        snapshot = self._capture_guard("same-session-newest-wins")
        if snapshot.stats.get("Web WS active") != 1:
            raise SoakAbort(f"same-session replacement left {snapshot.stats.get('Web WS active')} active clients instead of 1")
        self._close_all()
        self._wait_recovered("same-session-cleanup")
        return {"name": "same-session-newest-wins", "oldServerClosed": True, "settledActive": 1}

    def _stage_stale(self, count: int) -> dict[str, Any]:
        stage_deadline = time.monotonic() + self.args.stale_timeout
        stale = []
        connected_at = []
        for _index in range(min(count, max(1, self.capacity))):
            client = RawWebSocket.connect(self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=False)
            stale.append(client)
            if client.accepted:
                self.open_clients.append(client)
                connected_at.append(time.monotonic())
        accepted = [client for client in stale if client.accepted]
        if len(accepted) != len(stale):
            rejected = [f"HTTP {client.http_status}: {client.error}" for client in stale if not client.accepted]
            raise SoakAbort(f"clean stale-client admission accepted only {len(accepted)}/{len(stale)}: {rejected}")

        def observe(client: RawWebSocket, connected: float) -> tuple[bool, float]:
            remaining = max(0.0, stage_deadline - time.monotonic())
            did_close = client.wait_for_server_close(remaining)
            return did_close, time.monotonic() - connected

        with concurrent.futures.ThreadPoolExecutor(max_workers=len(accepted)) as pool:
            futures = [pool.submit(observe, client, connected) for client, connected in zip(accepted, connected_at)]
            observations = [future.result() for future in futures]
        closed = [observation[0] for observation in observations]
        close_ages = [observation[1] for observation in observations]
        for did_close, age in observations:
            if did_close and age < self.args.stale_min_close_seconds:
                raise SoakAbort(
                    f"no-pong client closed after only {age:.2f}s; too early to attribute to the 35s firmware stale policy"
                )
        if not all(closed):
            raise SoakAbort(f"firmware did not evict all no-pong clients within {self.args.stale_timeout}s")
        missing_heartbeats = [
            index
            for index, client in enumerate(accepted)
            if client.ping_frames_received < 1 or client.last_ping_payload != b"FNC"
        ]
        if missing_heartbeats:
            raise SoakAbort(
                "stale close occurred without observing the firmware Ping/FNC heartbeat on client(s) "
                + ", ".join(str(index) for index in missing_heartbeats)
            )
        self._close_all()
        self._wait_recovered("stale-cleanup")
        return {
            "name": "no-pong-stale-eviction",
            "accepted": len(accepted),
            "serverClosed": sum(closed),
            "closeAgesSeconds": [round(age, 3) for age in close_ages],
            "pingFramesReceived": [client.ping_frames_received for client in accepted],
            "lastPingPayloadHex": [
                client.last_ping_payload.hex() if client.last_ping_payload is not None else None for client in accepted
            ],
        }

    def _recover_and_rediscover_websocket_capacity(self, label: str) -> tuple[int, list[dict[str, Any]], Snapshot]:
        if self.open_clients:
            raise SoakAbort(f"{label}: internal error: WebSocket clients were still open before recovery")

        self.recorder.event(
            "static-capacity-recovery-quiet",
            label=label,
            quietSeconds=CHURN_BACKPRESSURE_QUIET_SECONDS,
            initialCapacity=self.capacity,
        )
        time.sleep(CHURN_BACKPRESSURE_QUIET_SECONDS)

        attempts: list[dict[str, Any]] = []
        first = RawWebSocket.connect(
            self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True
        )
        attempts.append(
            {
                "attempt": 1,
                "accepted": first.accepted,
                "httpStatus": first.http_status,
                "error": first.error,
            }
        )
        if not first.accepted:
            first.close(graceful=False)
            raise SoakAbort(
                f"{label} did not regain even one WebSocket after "
                f"{CHURN_BACKPRESSURE_QUIET_SECONDS}s without polling "
                f"(HTTP {first.http_status}, {first.error})"
            )
        self.open_clients.append(first)

        try:
            for attempt in range(2, self.args.max_connections + 1):
                client = RawWebSocket.connect(
                    self.args.host, self.args.port, timeout=self.args.ws_timeout, auto_pong=True
                )
                attempts.append(
                    {
                        "attempt": attempt,
                        "accepted": client.accepted,
                        "httpStatus": client.http_status,
                        "error": client.error,
                    }
                )
                if client.accepted:
                    self.open_clients.append(client)
                else:
                    client.close(graceful=False)

            current_capacity = len(self.open_clients)
            snapshot = self._capture_guard(label + "-rediscovered")
            if snapshot.stats.get("Web WS active") != current_capacity:
                raise SoakAbort(
                    f"{label} capacity accounting mismatch: host holds {current_capacity}, telemetry reports "
                    f"{snapshot.stats.get('Web WS active')}"
                )
            self.recorder.event(
                "static-capacity-rediscovered",
                label=label,
                initialCapacity=self.capacity,
                currentCapacity=current_capacity,
                capacityDegradation=max(0, self.capacity - current_capacity),
                attempts=attempts,
                snapshot=snapshot.to_dict(),
            )
            return current_capacity, attempts, snapshot
        except BaseException:
            self._close_all()
            raise

    def _stage_static(self, rounds: int) -> dict[str, Any]:
        before = self._capture_guard("static-before")
        # Release the in-band ESP420 observer before trying to prove that the
        # file stream itself can be admitted and held under backpressure.
        time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)
        first_chunk = threading.Event()
        release_first_chunk = threading.Event()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            positive_future = pool.submit(
                slow_static_get,
                self.args.host,
                self.args.port,
                "/",
                self.args.http_timeout,
                0.0,
                first_chunk_event=first_chunk,
                first_chunk_release_event=release_first_chunk,
                receive_buffer_bytes=1024,
            )
            if not first_chunk.wait(min(2.0, self.args.http_timeout)):
                positive_result = positive_future.result()
                raise SoakAbort(
                    "positive static GET did not receive a first response chunk: "
                    + str(positive_result.get("error"))
                )
            try:
                state_during_transfer = self._capture_light_state("static-positive-active")
            finally:
                release_first_chunk.set()
            positive_result = positive_future.result()
        if (
            positive_result.get("status") != 200
            or positive_result.get("bodyBytes", 0) <= 0
            or not positive_result.get("complete")
        ):
            raise SoakAbort(
                "positive static GET did not complete a complete HTTP 200 response with a non-empty body: "
                + json.dumps(positive_result, sort_keys=True)
            )
        positive_cleanup = self._wait_recovered("static-positive-cleanup")
        file_start_delta = positive_cleanup.stats.get("Web file starts", 0) - before.stats.get("Web file starts", 0)
        file_completion_delta = positive_cleanup.stats.get("Web file completions", 0) - before.stats.get(
            "Web file completions", 0
        )
        if file_start_delta != 1 or file_completion_delta != 1:
            raise SoakAbort(
                "positive static GET did not produce exactly one file-stream lifecycle "
                f"(starts +{file_start_delta}, completions +{file_completion_delta})"
            )
        current_capacity, capacity_attempts, before = self._recover_and_rediscover_websocket_capacity(
            "static-positive-cleanup"
        )
        # Capacity telemetry is itself an in-band AsyncTCP client.  Let it
        # leave the live transport set before adding Static GET pressure.
        time.sleep(IN_BAND_CLIENT_RELEASE_SECONDS)
        results = []
        try:
            for round_number in range(1, rounds + 1):
                paths = ["/" if index % 2 == 0 else f"/__codex_soak_missing_{round_number}_{index}" for index in range(8)]
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    results.extend(
                        pool.map(
                            lambda path: slow_static_get(
                                self.args.host, self.args.port, path, self.args.http_timeout, self.args.static_read_delay
                            ),
                            paths,
                        )
                    )
                if round_number % 10 == 0 or round_number == rounds:
                    self._capture_guard(f"static-{round_number}")
        finally:
            self._close_all()
        after = self._wait_recovered("static-cleanup")
        disconnected = sum(result["status"] == 0 for result in results)
        file_rejection_delta = after.stats.get("Web file rejections", 0) - before.stats.get("Web file rejections", 0)
        accept_rejection_delta = after.stats.get("Async TCP accept admission rejections", 0) - before.stats.get(
            "Async TCP accept admission rejections", 0
        )
        unexpected_statuses = sorted({result["status"] for result in results if result["status"] not in (0, 200, 304, 404)})
        if unexpected_statuses:
            raise SoakAbort(f"static GET pressure returned unexpected HTTP statuses: {unexpected_statuses}")
        validate_static_disconnect_attribution(
            disconnected,
            file_rejections=file_rejection_delta,
            accept_rejections=accept_rejection_delta,
        )
        return {
            "name": "mixed-ws-static-get-pressure",
            "positiveTransfer": {
                **positive_result,
                "stateDuringTransfer": state_during_transfer,
                "fileStartDelta": file_start_delta,
                "fileCompletionDelta": file_completion_delta,
            },
            "rounds": rounds,
            "requests": len(results),
            "initialDiscoveredCapacity": self.capacity,
            "currentCapacity": current_capacity,
            "capacityDegradation": max(0, self.capacity - current_capacity),
            "capacityAttempts": capacity_attempts,
            "disconnected": disconnected,
            "fileRejectionDelta": file_rejection_delta,
            "acceptAdmissionRejectionDelta": accept_rejection_delta,
            "statusCounts": {str(status): sum(result["status"] == status for result in results) for status in sorted({r['status'] for r in results})},
        }

    def run(self) -> None:
        baseline = self.http.capture_snapshot()
        errors = validate_preflight(baseline, self.args.expected_hardening_id)
        if errors:
            raise SoakAbort("preflight rejected: " + "; ".join(errors))
        self.baseline = baseline
        self.last_snapshot = baseline
        self.recorder.report["baseline"] = baseline.to_dict()
        self.recorder.event("preflight-pass", snapshot=baseline.to_dict())

        profile = PROFILE[self.args.profile]
        if self.args.profile == "parallel-repro":
            stage_functions = [lambda: self._stage_parallel(profile["parallelCycles"])]
        else:
            stage_functions = [
                self._stage_ramp,
                self._stage_same_session,
                lambda: self._stage_churn(profile["churnCycles"]),
                lambda: self._stage_early_rst(profile["earlyRstRounds"]),
                self._stage_aborted_upgrades,
                lambda: self._stage_partial_http(profile["partialRounds"]),
                lambda: self._stage_parallel(profile["parallelCycles"]),
                lambda: self._stage_stale(profile["staleClients"]),
                lambda: self._stage_static(profile["staticRounds"]),
            ]
        for stage_index, stage_function in enumerate(stage_functions):
            stage = stage_function()
            self.recorder.report["stages"].append(stage)
            self.recorder.event("stage-complete", stage=stage)
            self._capture_full_guard(stage["name"] + "-identity")
            if stage_index + 1 < len(stage_functions):
                self._quiet_transport_recovery(stage["name"] + "-to-next")
        self._wait_recovered("final-recovery", timeout=30)
        final_identity = self.http.capture_snapshot()
        identity_errors = validate_continuity(baseline, final_identity) + _resource_errors(final_identity.stats)
        if identity_errors:
            raise SoakAbort("final identity checkpoint rejected: " + "; ".join(identity_errors))
        final = self._wait_postflight_stable(final_identity)
        self.recorder.finish("PASS", capacity=self.capacity, final=final.to_dict())

    def cleanup(self) -> None:
        self._close_all()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="FluidNC IP/hostname")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--profile", choices=sorted(PROFILE), default="smoke")
    parser.add_argument("--max-connections", type=int, choices=range(1, 9), default=8)
    parser.add_argument("--http-timeout", type=float, default=8.0)
    parser.add_argument("--ws-timeout", type=float, default=5.0)
    parser.add_argument("--stale-timeout", type=float, default=60.0)
    parser.add_argument("--stale-min-close-seconds", type=float, default=30.0)
    parser.add_argument("--partial-hold-seconds", type=float, default=2.0)
    parser.add_argument("--static-read-delay", type=float, default=0.01)
    parser.add_argument(
        "--inter-stage-quiet-seconds",
        type=float,
        default=CHURN_BACKPRESSURE_QUIET_SECONDS,
        help="poll-free LwIP TIME_WAIT recovery interval after each identity checkpoint",
    )
    parser.add_argument(
        "--post-recovery-quiet-seconds",
        type=float,
        default=FIRST_SOCKET_RECOVERY_REARM_SECONDS,
        help="poll-free interval after the one-shot probe closes so the first-client recovery gate rearms",
    )
    parser.add_argument("--expected-hardening-id", required=True)
    parser.add_argument(
        "--execute-live",
        action="store_true",
        help="required acknowledgement that the target is an idle, non-production diagnostic board",
    )
    return parser


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.execute_live:
        parser.error("live network execution is disabled unless --execute-live is supplied")
    if args.output.exists():
        parser.error("--output must name a new directory; refusing to overwrite evidence")
    if (
        args.http_timeout <= 0
        or args.ws_timeout <= 0
        or args.stale_timeout <= 0
        or args.stale_min_close_seconds < 0
        or args.stale_min_close_seconds >= args.stale_timeout
        or args.partial_hold_seconds <= 0
    ):
        parser.error("timeouts must be positive")
    if args.static_read_delay < 0:
        parser.error("--static-read-delay must be non-negative")
    if args.inter_stage_quiet_seconds < 0:
        parser.error("--inter-stage-quiet-seconds must be non-negative")
    if args.post_recovery_quiet_seconds < 0:
        parser.error("--post-recovery-quiet-seconds must be non-negative")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    arguments = {
        "host": args.host,
        "port": args.port,
        "profile": args.profile,
        "maxConnections": args.max_connections,
        "expectedHardeningId": args.expected_hardening_id,
        "httpTimeoutSeconds": args.http_timeout,
        "webSocketTimeoutSeconds": args.ws_timeout,
        "staleTimeoutSeconds": args.stale_timeout,
        "staleMinimumCloseSeconds": args.stale_min_close_seconds,
        "partialHoldSeconds": args.partial_hold_seconds,
        "staticReadDelaySeconds": args.static_read_delay,
        "interStageQuietSeconds": args.inter_stage_quiet_seconds,
        "postRecoveryQuietSeconds": args.post_recovery_quiet_seconds,
    }
    recorder = EvidenceRecorder(args.output, arguments)
    harness = SoakHarness(args, recorder)
    exit_code = 0
    pending_interrupt: KeyboardInterrupt | None = None
    try:
        harness.run()
        print(recorder.report_path.resolve())
    except BaseException as error:
        failure = traceback.format_exc()
        try:
            recorder.event("abort", errorType=type(error).__name__, error=str(error), traceback=failure)
            recorder.finish(
                "ABORT", errorType=type(error).__name__, error=str(error), traceback=failure, capacity=harness.capacity
            )
        except Exception as report_error:
            print(f"ABORT report persistence also failed: {report_error}")
        print(f"ABORT: {error}")
        print(recorder.report_path.resolve())
        exit_code = 2
        if isinstance(error, KeyboardInterrupt):
            pending_interrupt = error
    finally:
        harness.cleanup()
    if pending_interrupt is not None:
        raise pending_interrupt
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
