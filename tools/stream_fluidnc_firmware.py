#!/usr/bin/env python3
"""Send one FluidNC multipart firmware upload with small, paced TCP writes."""

from __future__ import annotations

import argparse
import hashlib
import json
import socket
import time
from pathlib import Path


def send_all(sock: socket.socket, payload: bytes) -> None:
    view = memoryview(payload)
    while view:
        written = sock.send(view)
        if written <= 0:
            raise ConnectionError("socket closed while sending multipart body")
        view = view[written:]


def build_multipart_prefix(
    boundary: str,
    firmware_name: str,
    firmware_size: int,
    file_field_name: str = "myfile[]",
) -> bytes:
    if file_field_name not in ("myfile[]", "myfiles"):
        raise ValueError(f"unsupported firmware file field: {file_field_name}")
    firmware_path = f"/{firmware_name}"
    return (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="path"\r\n\r\n'
        f"/\r\n"
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="createPath"\r\n\r\n'
        f"true\r\n"
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="{firmware_path}S"\r\n\r\n'
        f"{firmware_size}\r\n"
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="{file_field_name}"; filename="{firmware_path}"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode("ascii")


def transmit_firmware(
    sock: socket.socket,
    *,
    request: bytes,
    prefix: bytes,
    firmware_chunks,
    suffix: bytes,
    delay_ms: float,
    progress: dict[str, int] | None = None,
) -> tuple[int, bytes]:
    """Send one Content-Length request and wait for its HTTP response.

    Do not half-close the TCP socket after the body.  ESPAsyncWebServer can
    process that FIN as a disconnect before dispatching the request-complete
    handler that sends the response and schedules the firmware reboot.
    """
    if progress is None:
        progress = {}
    progress["fileBytesSent"] = 0
    response = bytearray()

    send_all(sock, request)
    send_all(sock, prefix)
    for chunk in firmware_chunks:
        send_all(sock, chunk)
        progress["fileBytesSent"] += len(chunk)
        if delay_ms:
            time.sleep(delay_ms / 1000.0)
    send_all(sock, suffix)

    while True:
        block = sock.recv(4096)
        if not block:
            break
        response.extend(block)
    return progress["fileBytesSent"], bytes(response)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("firmware", type=Path)
    parser.add_argument("--host", default="192.0.2.2")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--chunk-size", type=int, default=1024)
    parser.add_argument("--delay-ms", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument(
        "--file-field-name",
        choices=("myfile[]", "myfiles"),
        default="myfile[]",
        help="multipart file field: live WebUI default or legacy checked-in v4 alias",
    )
    parser.add_argument("--expected-sha256", required=True)
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    size = firmware.stat().st_size
    digest = hashlib.sha256(firmware.read_bytes()).hexdigest().upper()
    if digest != args.expected_sha256.upper():
        raise SystemExit(f"firmware hash mismatch: expected {args.expected_sha256}, got {digest}")
    if args.chunk_size < 256 or args.chunk_size > 16 * 1024:
        raise SystemExit("chunk size must be between 256 and 16384 bytes")

    boundary = "----fluidnc-codex-diagnostic-20260820"
    prefix = build_multipart_prefix(boundary, firmware.name, size, args.file_field_name)
    suffix = f"\r\n--{boundary}--\r\n".encode("ascii")
    content_length = len(prefix) + size + len(suffix)
    request = (
        f"POST /updatefw HTTP/1.1\r\n"
        f"Host: {args.host}\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Content-Length: {content_length}\r\n"
        f"Connection: close\r\n\r\n"
    ).encode("ascii")

    started = time.monotonic()
    sent_file = 0
    response = bytearray()
    upload_progress = {"fileBytesSent": 0}
    error = None
    try:
        with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
            sock.settimeout(args.timeout)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with firmware.open("rb") as stream:
                chunks = iter(lambda: stream.read(args.chunk_size), b"")
                sent_file, wire_response = transmit_firmware(
                    sock,
                    request=request,
                    prefix=prefix,
                    firmware_chunks=chunks,
                    suffix=suffix,
                    delay_ms=args.delay_ms,
                    progress=upload_progress,
                )
                response.extend(wire_response)
    except (BrokenPipeError, ConnectionError, ConnectionResetError, OSError) as exc:
        sent_file = upload_progress["fileBytesSent"]
        error = f"{type(exc).__name__}: {exc}"

    result = {
        "firmware": str(firmware),
        "sha256": digest,
        "size": size,
        "fileFieldName": args.file_field_name,
        "fileBytesSent": sent_file,
        "elapsedSeconds": round(time.monotonic() - started, 3),
        "response": response.decode("utf-8", errors="replace"),
        "error": error,
    }
    print(json.dumps(result, indent=2))
    return 0 if error is None and response.startswith(b"HTTP/1.1 200") else 1


if __name__ == "__main__":
    raise SystemExit(main())
