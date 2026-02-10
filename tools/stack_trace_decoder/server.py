#!/usr/bin/env python3
import io
import os
import re
import shutil
import subprocess
import tempfile
import urllib.request
from pathlib import Path

from flask import Flask, jsonify, request

app = Flask(__name__)
app.config["MAX_CONTENT_LENGTH"] = 100 * 1024 * 1024  # 100 MB

ADDRESS_RE = re.compile(r"0x[0-9a-fA-F]{8}")


def extract_addresses(text: str) -> list:
    if not text:
        return []
    return ADDRESS_RE.findall(text)


def find_addr2line() -> str | None:
    env_path = os.environ.get("ADDR2LINE")
    if env_path and os.path.exists(env_path):
        return env_path
    for candidate in (
        "xtensa-esp32-elf-addr2line",
        "xtensa-esp32s3-elf-addr2line",
        "addr2line",
    ):
        found = shutil.which(candidate)
        if found:
            return found
    return None


def download_elf(url: str, dest: Path) -> None:
    with urllib.request.urlopen(url) as resp:
        if resp.status >= 400:
            raise RuntimeError(f"Failed to download ELF: HTTP {resp.status}")
        data = resp.read()
        dest.write_bytes(data)


def decode_addresses(elf_path: Path, addresses: list) -> dict:
    addr2line = find_addr2line()
    if not addr2line:
        raise RuntimeError("addr2line not found. Set ADDR2LINE or install toolchain.")

    if not addresses:
        raise RuntimeError("No addresses to decode.")

    cmd = [addr2line, "-e", str(elf_path), "-f", "-C", *addresses]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "addr2line failed")

    return {
        "addr2line": addr2line,
        "decoded": result.stdout.strip(),
    }


@app.after_request
def add_cors_headers(response):
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "Content-Type"
    response.headers["Access-Control-Allow-Methods"] = "POST, OPTIONS"
    return response


@app.route("/decode", methods=["POST", "OPTIONS"])
def decode():
    if request.method == "OPTIONS":
        return ("", 204)

    try:
        stack_trace = ""
        addresses = []
        elf_url = ""

        if request.is_json:
            payload = request.get_json() or {}
            stack_trace = payload.get("stack_trace", "")
            addresses = payload.get("addresses", [])
            elf_url = payload.get("elf_url", "")
        else:
            stack_trace = request.form.get("stack_trace", "")
            addresses_text = request.form.get("addresses", "")
            elf_url = request.form.get("elf_url", "")
            if addresses_text:
                addresses = ADDRESS_RE.findall(addresses_text)

        if not addresses:
            addresses = extract_addresses(stack_trace)

        elf_file = request.files.get("elf")

        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir_path = Path(tmpdir)
            elf_path = tmpdir_path / "firmware.elf"

            if elf_file:
                elf_path.write_bytes(elf_file.read())
                elf_source = "upload"
            elif elf_url:
                download_elf(elf_url, elf_path)
                elf_source = elf_url
            else:
                return jsonify({"error": "Missing ELF file or elf_url"}), 400

            result = decode_addresses(elf_path, addresses)

        return jsonify(
            {
                "addresses": addresses,
                "decoded": result["decoded"],
                "addr2line": result["addr2line"],
                "elf_source": elf_source,
            }
        )
    except Exception as exc:
        return jsonify({"error": str(exc)}), 500


if __name__ == "__main__":
    host = os.environ.get("DECODER_HOST", "127.0.0.1")
    port = int(os.environ.get("DECODER_PORT", "5000"))
    app.run(host=host, port=port)