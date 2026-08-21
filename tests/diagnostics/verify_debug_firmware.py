#!/usr/bin/env python3
"""Artifact-level verification for the ESP32 debug firmware build."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


EXPECTED_DEPENDENCY_RESOLUTION = {
    "platformCommit": "87cbed0ab9e66a78e99a5ddfa19c6bf900a79bc7",
    "frameworkVersion": "3.20017.241212+sha.dcc1105b",
    "AsyncTCP": "3.5.0",
    "TMCStepper": "0.7.3",
    "OLED": "4.4.1",
    "JsonStreamingParserCommit": "ddb90a2e4f798a62a2624f2ed21e8c2c64ae60e0",
    "ESPAsyncWebServerCommit": "d009eff9ee94f92beccdf5812d89ec79aa44a6c1",
    "tool-esptoolpy": "2.41100.0",
    "toolchain-xtensa-esp32": "8.4.0+2021r2-patch5",
    "platformContentSha256": "69E8D1514F14565F6B06A091CF3F63AAD9798E45F1D63ADA3E6C0CE86BF4AC06",
    "frameworkContentSha256": "81114B604D848B728C38522C649BDEBF7CA904E07195FC5A2DE8A830936E7978",
    "AsyncTCPContentSha256": "083E528A2E5B5A77EB4B13BB31F9DC2C029A2AD492154A9D3B8EB63918AAFE7E",
    "TMCStepperContentSha256": "3C2057842BBE6AB2DB86836C9C218902C52C37AE5EB225A26F72801B0DA2B536",
    "OLEDContentSha256": "4594F468C24207402B799E2E8D159AE3B8D600A06BB625CB8DEFB6544BE6B044",
    "JsonStreamingParserContentSha256": "B2F42D517C62454571287DDB56AFDCF401BA4B34624B4E1804C6C03910D7EF85",
    "ESPAsyncWebServerContentSha256": "C1A63F94DB91DF3D851AB6C6EC619F9B831CB9D7C032CF771FC88B3A2D2C3368",
}
EXPECTED_PLATFORMIO_RUNTIME = {
    "pythonSha256": "E4C6FFE75947E5FCAF5B6A604776A19882E19B4315F2AC58AD460DCEF2E5609A",
    "pythonVersion": "3.11.7 (tags/v3.11.7:fa7a6f2, Dec  4 2023, 19:24:49) [MSC v.1937 64 bit (AMD64)]",
    "platformioVersion": "6.1.18",
    "packageFileCount": 285,
    "packageAggregateSha256": "14E10C470A2D1160CC6AFF47A06D5BE4D24536DA4E0322925F1B1DA0891B7D78",
    "distInfoFileCount": 8,
    "distInfoAggregateSha256": "F166EC8D3338E937AF69AAD5390205CF3B473671BF8ECF7AAEA36B03F29CF771",
}

REJECT_ABORT_TIMING_MARKERS = (
    "async_web_resource_reject_abort_calls_counter.fetch_add(1, std::memory_order_relaxed);",
    "const uint32_t abort_started_us = micros();",
    "request->abort();",
    "const uint32_t abort_elapsed_us = static_cast<uint32_t>(micros() - abort_started_us);",
    "uint32_t observed = async_web_resource_reject_abort_max_us_counter.load(std::memory_order_relaxed);",
    "while (observed < abort_elapsed_us &&",
    "!async_web_resource_reject_abort_max_us_counter.compare_exchange_weak(",
    "observed, abort_elapsed_us, std::memory_order_relaxed, std::memory_order_relaxed))",
)


def require_ordered_markers(text: str, markers: tuple[str, ...], label: str) -> None:
    position = 0
    for marker in markers:
        found = text.find(marker, position)
        assert found >= 0, f"{label} is missing or reorders required marker: {marker}"
        position = found + len(marker)
PLATFORMIO_RUNTIME_PROBE = (
    "import importlib.metadata,json,platformio,sys; from pathlib import Path; "
    "dist=importlib.metadata.distribution('platformio'); "
    "print(json.dumps({'pythonPath':str(Path(sys.executable).resolve()),"
    "'pythonVersion':sys.version,'platformioVersion':importlib.metadata.version('platformio'),"
    "'packagePath':str(Path(platformio.__file__).resolve().parent),"
    "'distInfoPath':str(Path(dist._path).resolve())},sort_keys=True))"
)


def find_tool(packages: Path, name: str) -> Path:
    matches = sorted(packages.glob(f"**/{name}"))
    if not matches:
        raise AssertionError(f"tool not found below {packages}: {name}")
    return matches[0]


def default_platformio_packages() -> Path:
    """Resolve packages from the same PlatformIO core that built the image.

    PlatformIO honors PLATFORMIO_CORE_DIR.  A verifier that silently falls
    back to the user's mutable global cache can validate a different framework
    than the one used by an isolated build.
    """
    core_dir = os.environ.get("PLATFORMIO_CORE_DIR")
    if core_dir:
        return Path(core_dir) / "packages"
    return Path.home() / ".platformio" / "packages"


def undefined_symbols(nm: Path, object_file: Path) -> set[str]:
    result = subprocess.run(
        [str(nm), "-C", "-u", str(object_file)],
        check=True,
        capture_output=True,
        text=True,
    )
    symbols: set[str] = set()
    for line in result.stdout.splitlines():
        stripped = line.strip()
        if stripped.startswith("U "):
            symbols.add(stripped[2:].strip())
    return symbols


def symbol_listing(nm: Path, binary: Path) -> str:
    result = subprocess.run(
        [str(nm), "-C", "--defined-only", str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout


def string_listing(strings: Path, binary: Path) -> str:
    result = subprocess.run(
        [str(strings), str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout


def _read_json(path: Path, label: str) -> dict:
    assert path.is_file(), f"{label} metadata is missing: {path}"
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AssertionError(f"{label} metadata is invalid: {path}: {error}") from error


def _git_head(path: Path, label: str) -> str:
    assert path.is_dir(), f"{label} Git checkout is missing: {path}"
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def _assert_clean_git(path: Path, label: str) -> None:
    result = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain", "--untracked-files=all"],
        check=True,
        capture_output=True,
        text=True,
    )
    assert not result.stdout.strip(), f"{label} Git checkout is dirty: {result.stdout.strip()}"


def directory_aggregate(path: Path) -> tuple[int, str]:
    assert path.is_dir(), f"dependency content directory is missing: {path}"
    files = sorted(
        (
            candidate
            for candidate in path.rglob("*")
            if candidate.is_file()
            and not any(part in {".git", "__pycache__"} for part in candidate.relative_to(path).parts)
            and candidate.suffix != ".pyc"
        ),
        key=lambda candidate: candidate.relative_to(path).as_posix(),
    )
    aggregate = hashlib.sha256()
    for candidate in files:
        file_hash = hashlib.sha256()
        size = 0
        with candidate.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                file_hash.update(chunk)
                size += len(chunk)
        aggregate.update(candidate.relative_to(path).as_posix().encode("utf-8"))
        aggregate.update(b"\0")
        aggregate.update(str(size).encode("ascii"))
        aggregate.update(b"\0")
        aggregate.update(file_hash.digest())
        aggregate.update(b"\n")
    return len(files), aggregate.hexdigest().upper()


def file_sha256(path: Path, label: str) -> str:
    assert path.is_file(), f"{label} is missing: {path}"
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def input_manifest_identity(manifest: dict) -> dict:
    manifest_git = manifest.get("git")
    assert isinstance(manifest_git, dict) and isinstance(manifest_git.get("commit"), str), (
        "firmware build-input manifest Git identity is missing"
    )
    fields = (
        "inputFileCount",
        "inputAggregateSha256",
        "gitlinkCount",
        "gitlinkAggregateSha256",
        "overlayFileCount",
        "overlayAggregateSha256",
    )
    identity = {"sourceGitCommit": manifest_git["commit"]}
    for field in fields:
        assert field in manifest, f"firmware build-input manifest is missing {field}"
        identity[field] = manifest[field]
    return identity


def current_input_manifest_identity(root: Path) -> dict:
    tool_path = root / "tools" / "manifest_firmware_build_inputs.py"
    assert tool_path.is_file(), f"input-manifest tool is missing: {tool_path}"
    spec = importlib.util.spec_from_file_location("verify_firmware_build_inputs", tool_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None, f"could not load input-manifest tool: {tool_path}"
    spec.loader.exec_module(module)
    manifest = module.build_manifest(root)
    assert isinstance(manifest, dict), "input-manifest tool returned an invalid manifest"
    return input_manifest_identity(manifest)


def platformio_runtime_identity(executable: str) -> dict[str, str | int]:
    candidate = Path(executable)
    resolved = candidate.resolve() if candidate.is_file() else None
    if resolved is None:
        found = shutil.which(executable)
        assert found, f"recorded PlatformIO executable is missing: {executable}"
        resolved = Path(found).resolve()
    runtime_python = resolved.parent / "python.exe"
    assert runtime_python.is_file(), f"recorded PlatformIO launcher lacks its sibling Python interpreter: {resolved}"
    result = subprocess.run(
        [str(runtime_python), "-c", PLATFORMIO_RUNTIME_PROBE], check=True, capture_output=True, text=True
    )
    try:
        probe = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"recorded PlatformIO runtime probe returned invalid JSON: {resolved}: {error}") from error
    assert isinstance(probe, dict) and probe.get("pythonPath") == str(runtime_python.resolve()), (
        f"recorded PlatformIO runtime probe did not use the expected interpreter: {resolved}"
    )
    package = Path(str(probe.get("packagePath", ""))).resolve()
    dist_info = Path(str(probe.get("distInfoPath", ""))).resolve()
    package_files, package_aggregate = directory_aggregate(package)
    dist_files, dist_aggregate = directory_aggregate(dist_info)
    return {
        "launcherPath": str(resolved),
        "launcherSha256": file_sha256(resolved, "recorded PlatformIO launcher"),
        "pythonPath": str(runtime_python.resolve()),
        "pythonSha256": file_sha256(runtime_python, "recorded PlatformIO Python"),
        "pythonVersion": str(probe.get("pythonVersion", "")),
        "platformioVersion": str(probe.get("platformioVersion", "")),
        "packagePath": str(package),
        "packageFileCount": package_files,
        "packageAggregateSha256": package_aggregate,
        "distInfoPath": str(dist_info),
        "distInfoFileCount": dist_files,
        "distInfoAggregateSha256": dist_aggregate,
    }


def verify_clean_build_receipt(root: Path, packages: Path) -> None:
    """Require a clean-build receipt for the exact PlatformIO core under test."""
    output = root / ".pio" / "build" / "wifi"
    receipt_path = output / "firmware-build-receipt.json"
    input_manifest_path = output / "firmware-build-inputs.json"
    firmware_bin = output / "firmware.bin"
    firmware_elf = output / "firmware.elf"
    receipt = _read_json(receipt_path, "clean firmware build receipt")

    assert receipt.get("schemaVersion") == 1, "clean firmware build receipt schema is unsupported"
    assert receipt.get("environment") == "wifi", "clean firmware build receipt is not for wifi"
    packages = packages.resolve()
    assert receipt.get("packagesDir") == str(packages), (
        "clean firmware build receipt packages path differs from the verifier packages path"
    )
    assert receipt.get("platformioCoreDir") == str(packages.parent), (
        "clean firmware build receipt core differs from the verifier packages core"
    )
    recorded_runtime = receipt.get("platformioRuntime")
    assert isinstance(recorded_runtime, dict), "clean firmware build receipt PlatformIO runtime is missing"
    assert recorded_runtime == platformio_runtime_identity(str(recorded_runtime.get("launcherPath", ""))), (
        "clean firmware build receipt PlatformIO runtime differs from the current runtime"
    )
    for field, expected in EXPECTED_PLATFORMIO_RUNTIME.items():
        assert recorded_runtime.get(field) == expected, f"PlatformIO runtime {field} differs from the expected tool identity"
    assert receipt.get("firmwareBinSha256") == file_sha256(firmware_bin, "firmware.bin"), (
        "clean firmware build receipt firmware.bin hash differs from the artifact"
    )
    assert receipt.get("firmwareElfSha256") == file_sha256(firmware_elf, "firmware.elf"), (
        "clean firmware build receipt firmware.elf hash differs from the artifact"
    )
    assert receipt_path.stat().st_mtime_ns >= max(
        firmware_bin.stat().st_mtime_ns, firmware_elf.stat().st_mtime_ns
    ), "clean firmware build receipt predates the artifact"

    manifest = _read_json(input_manifest_path, "firmware build-input manifest")
    assert receipt.get("inputManifestSha256") == file_sha256(
        input_manifest_path, "firmware build-input manifest"
    ), "clean firmware build receipt input-manifest hash differs from the manifest"
    manifest_identity = input_manifest_identity(manifest)
    assert receipt.get("inputManifestInputAggregateSha256") == manifest_identity["inputAggregateSha256"], (
        "clean firmware build receipt input-manifest aggregate differs from the manifest"
    )
    assert receipt.get("preBuildInputIdentity") == manifest_identity, (
        "clean firmware build receipt pre-build source identity differs from the post-build manifest"
    )
    assert current_input_manifest_identity(root) == manifest_identity, (
        "firmware source inputs differ from the receipt input manifest"
    )
    current_commit = _git_head(root, "firmware source")
    assert receipt.get("sourceGitCommit") == current_commit, (
        "clean firmware build receipt source commit differs from the source worktree"
    )
    assert receipt.get("inputManifestGitCommit") == current_commit == manifest_identity["sourceGitCommit"], (
        "clean firmware build receipt and input manifest do not bind the current source commit"
    )
    _assert_clean_git(root, "firmware source")

    builder = root / "tools" / "build_debug_firmware.py"
    manifest_tool = root / "tools" / "manifest_firmware_build_inputs.py"
    assert receipt.get("builderScriptSha256") == file_sha256(builder, "clean-build wrapper"), (
        "clean firmware build receipt builder script differs from the source wrapper"
    )
    assert receipt.get("inputManifestToolSha256") == file_sha256(manifest_tool, "input-manifest tool"), (
        "clean firmware build receipt input-manifest tool differs from the source tool"
    )


def verify_dependency_resolution(
    root: Path,
    platformio_home: Path,
    expected: dict[str, str] = EXPECTED_DEPENDENCY_RESOLUTION,
) -> None:
    platform_commit = _git_head(platformio_home / "platforms" / "espressif32", "platform-espressif32")
    assert platform_commit == expected["platformCommit"], (
        f"platform-espressif32 resolved to {platform_commit}, expected {expected['platformCommit']}"
    )
    _assert_clean_git(platformio_home / "platforms" / "espressif32", "platform-espressif32")

    framework = _read_json(
        platformio_home / "packages" / "framework-arduinoespressif32" / "package.json",
        "framework-arduinoespressif32",
    )
    assert framework.get("version") == expected["frameworkVersion"], (
        "framework-arduinoespressif32 resolved to "
        f"{framework.get('version')}, expected {expected['frameworkVersion']}"
    )

    tool_versions = {
        "tool-esptoolpy": "tool-esptoolpy",
        "toolchain-xtensa-esp32": "toolchain-xtensa-esp32",
    }
    for expectation, directory in tool_versions.items():
        metadata = _read_json(platformio_home / "packages" / directory / "package.json", expectation)
        assert metadata.get("version") == expected[expectation], (
            f"{expectation} resolved to {metadata.get('version')}, expected {expected[expectation]}"
        )

    library_versions = {
        "AsyncTCP": "AsyncTCP",
        "TMCStepper": "TMCStepper",
        "OLED": "ESP8266 and ESP32 OLED driver for SSD1306 displays",
    }
    for label, directory in library_versions.items():
        metadata = _read_json(root / ".pio" / "libdeps" / "wifi" / directory / ".piopm", label)
        assert metadata.get("version") == expected[label], (
            f"{label} resolved to {metadata.get('version')}, expected {expected[label]}"
        )

    git_dependencies = {
        "JsonStreamingParserCommit": ("JsonStreamingParser", "JsonStreamingParser"),
        "ESPAsyncWebServerCommit": ("ESPAsyncWebServer", "ESPAsyncWebServer"),
    }
    for expectation, (directory, label) in git_dependencies.items():
        checkout = root / ".pio" / "libdeps" / "wifi" / directory
        commit = _git_head(checkout, label)
        assert commit == expected[expectation], (
            f"{label} resolved to {commit}, expected {expected[expectation]}"
        )
        if label == "JsonStreamingParser":
            _assert_clean_git(checkout, label)

    content_roots = {
        "platformContentSha256": (platformio_home / "platforms" / "espressif32", "platform-espressif32"),
        "frameworkContentSha256": (
            platformio_home / "packages" / "framework-arduinoespressif32",
            "framework-arduinoespressif32",
        ),
        "AsyncTCPContentSha256": (root / ".pio" / "libdeps" / "wifi" / "AsyncTCP", "AsyncTCP"),
        "TMCStepperContentSha256": (root / ".pio" / "libdeps" / "wifi" / "TMCStepper", "TMCStepper"),
        "OLEDContentSha256": (
            root / ".pio" / "libdeps" / "wifi" / "ESP8266 and ESP32 OLED driver for SSD1306 displays",
            "OLED",
        ),
        "JsonStreamingParserContentSha256": (
            root / ".pio" / "libdeps" / "wifi" / "JsonStreamingParser",
            "JsonStreamingParser",
        ),
        "ESPAsyncWebServerContentSha256": (
            root / ".pio" / "libdeps" / "wifi" / "ESPAsyncWebServer",
            "ESPAsyncWebServer post-patch",
        ),
    }
    for expectation, (directory, label) in content_roots.items():
        _count, aggregate = directory_aggregate(directory)
        assert aggregate == expected[expectation], (
            f"{label} content aggregate is {aggregate}, expected {expected[expectation]}"
        )


def verify_watchdog_calls(root: Path, packages: Path) -> None:
    object_file = root / ".pio" / "build" / "wifi" / "esp32" / "wdt.cpp.o"
    assert object_file.is_file(), f"missing build artifact: {object_file}"
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = undefined_symbols(nm, object_file)
    required = {"esp_task_wdt_add", "esp_task_wdt_reset"}
    missing = sorted(required - symbols)
    assert not missing, (
        "watchdog wrapper compiled without required ESP-IDF calls; "
        f"missing undefined symbols: {', '.join(missing)}"
    )


def verify_recovery_hooks(root: Path, packages: Path) -> None:
    firmware = root / ".pio" / "build" / "wifi" / "firmware.elf"
    assert firmware.is_file(), f"missing build artifact: {firmware}"
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = symbol_listing(nm, firmware)
    required = (
        "DebugRecovery::initialize_after_localfs_mount()",
        "DebugRecovery::previous_reset_was_crash()",
        "DebugRecovery::previous_phase()",
        "DebugRecovery::mark_phase(DebugRecovery::BootPhase)",
        "DebugRecovery::record_config_identity(char const*)",
        "DebugRecovery::serialize_journal_entry(",
        "DebugRecovery::start_debug_supervisor()",
    )
    missing = [symbol for symbol in required if symbol not in symbols]
    assert not missing, "firmware is missing recovery hooks: " + ", ".join(missing)


def verify_live_soak_identity(root: Path, packages: Path) -> None:
    marker = "v4.0.4-webhardening-20260821-r22"
    header = root / "FluidNC" / "src" / "DebugRecovery" / "DebugRecovery.h"
    sys_stats = root / "FluidNC" / "esp32" / "SysStats.cpp"
    assert marker in header.read_text(encoding="utf-8"), "diagnostic hardening marker is missing from source"
    stats_text = sys_stats.read_text(encoding="utf-8")
    assert '"Diagnostic hardening ID"' in stats_text, "ESP420 does not expose the hardening marker"
    assert '"Diagnostic boot sequence"' in stats_text, "ESP420 does not expose the boot sequence"
    assert '"Diagnostic uptime ms"' in stats_text, "ESP420 does not expose reboot-safe uptime"
    assert '"Diagnostic reset reason"' in stats_text, "ESP420 does not expose the reset reason"

    firmware = root / ".pio" / "build" / "wifi" / "firmware.elf"
    object_file = root / ".pio" / "build" / "wifi" / "esp32" / "SysStats.cpp.o"
    assert object_file.is_file(), f"missing build artifact: {object_file}"
    assert object_file.stat().st_mtime_ns >= max(header.stat().st_mtime_ns, sys_stats.stat().st_mtime_ns), (
        "SysStats object predates the live-soak identity source"
    )
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = symbol_listing(nm, firmware)
    assert "DebugRecovery::current_boot_sequence()" in symbols, "firmware ELF is missing the boot-sequence hook"
    strings = find_tool(packages, "xtensa-esp32-elf-strings.exe")
    assert marker in string_listing(strings, firmware), "firmware ELF is missing the exact hardening marker"


def verify_supervisor_uses_ordered_timing_sample(root: Path, packages: Path) -> None:
    object_file = (
        root
        / ".pio"
        / "build"
        / "wifi"
        / "src"
        / "DebugRecovery"
        / "DebugSupervisor.cpp.o"
    )
    assert object_file.is_file(), f"missing build artifact: {object_file}"
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = undefined_symbols(nm, object_file)
    required = (
        "DebugRecovery::capture_supervisor_timing",
        "DebugRecovery::supervisor_start_failure_should_restart",
    )
    missing = [prefix for prefix in required if not any(symbol.startswith(prefix) for symbol in symbols)]
    assert not missing, "compiled supervisor bypasses recovery policy: " + ", ".join(missing)


def verify_localfs_uses_no_autoformat_policy(root: Path, packages: Path) -> None:
    object_file = root / ".pio" / "build" / "wifi" / "esp32" / "localfs.cpp.o"
    assert object_file.is_file(), f"missing build artifact: {object_file}"
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = undefined_symbols(nm, object_file)
    required = "DebugRecovery::automatic_filesystem_format_allowed"
    assert any(symbol.startswith(required) for symbol in symbols), (
        "compiled LocalFS mount bypasses the no-autoformat policy; "
        f"missing undefined symbol prefix: {required}"
    )


def verify_retained_backtrace(root: Path) -> None:
    object_dir = root / ".pio" / "build" / "wifi" / "esp32"
    retained = object_dir / "coredump.c.o"
    no_op = object_dir / "backtrace.c.o"
    assert retained.is_file(), f"retained panic backtrace object is missing: {retained}"
    assert not no_op.exists(), f"no-op panic backtrace object was compiled unexpectedly: {no_op}"


def verify_retryable_websocket_close(root: Path, packages: Path) -> None:
    override = (root / "platformio_override.ini").read_text(encoding="utf-8")
    patch_script = "pre:tools/patch_espasyncwebserver_close.py"
    assert patch_script in override, f"wifi build does not enable {patch_script}"
    diagnostics_patch = "pre:tools/patch_espasyncwebserver_diagnostics.py"
    assert diagnostics_patch in override, f"wifi build does not enable {diagnostics_patch}"

    source = (
        root
        / ".pio"
        / "libdeps"
        / "wifi"
        / "ESPAsyncWebServer"
        / "src"
        / "AsyncWebSocket.cpp"
    )
    assert source.is_file(), f"patched ESPAsyncWebServer source is missing: {source}"
    source_text = source.read_text(encoding="utf-8")
    assert "FluidNC resource-pressure retry fix" in source_text, (
        "ESPAsyncWebServer close retry patch is absent from the dependency source"
    )
    assert "catch (...) {\n    _status = WS_CONNECTED;\n    throw;" in source_text, (
        "ESPAsyncWebServer close retry patch does not restore WS_CONNECTED"
    )
    assert "bool AsyncWebSocket::abort(uint32_t id)" in source_text, (
        "ESPAsyncWebServer is missing the ID-bound allocation-free abort path"
    )
    assert "FluidNC resource-pressure handshake rejection" in source_text and "request->abort();" in source_text, (
        "ESPAsyncWebServer handshake rejection can still allocate a status response instead of aborting"
    )
    for marker in (
        "FluidNC resource-pressure reject abort latency diagnostic",
        "async_web_resource_reject_abort_calls",
        "async_web_resource_reject_abort_max_us",
    ):
        assert marker in source_text, f"ESPAsyncWebServer reject-abort diagnostics missing: {marker}"
    require_ordered_markers(source_text, REJECT_ABORT_TIMING_MARKERS, "ESPAsyncWebServer reject-abort timing")
    websocket_header = source.with_name("AsyncWebSocket.h")
    assert "bool abort(uint32_t id);" in websocket_header.read_text(encoding="utf-8"), (
        "ESPAsyncWebServer header is missing the ID-bound abort declaration"
    )

    objects = list((root / ".pio" / "build" / "wifi").glob("lib*/ESPAsyncWebServer/AsyncWebSocket.cpp.o"))
    assert len(objects) == 1, f"expected one compiled AsyncWebSocket object, found {len(objects)}"
    object_file = objects[0]
    assert object_file.stat().st_mtime_ns >= source.stat().st_mtime_ns, (
        "AsyncWebSocket object predates the patched dependency source"
    )

    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    symbols = symbol_listing(nm, object_file)
    assert "AsyncWebSocketClient::close(unsigned short, char const*)" in symbols, (
        "compiled AsyncWebSocket object is missing the patched close implementation"
    )
    assert "AsyncWebSocket::abort(unsigned int)" in symbols, (
        "compiled AsyncWebSocket object is missing the ID-bound abort implementation"
    )
    for symbol in (
        "async_web_resource_reject_abort_calls",
        "async_web_resource_reject_abort_max_us",
    ):
        assert symbol in symbols, f"compiled AsyncWebSocket object is missing reject-abort diagnostics: {symbol}"

    header = source.parent / "ESPAsyncWebServer.h"
    request_source = source.parent / "WebRequest.cpp"
    assert "class AsyncRequestOwnerAllocator" in header.read_text(encoding="utf-8"), (
        "ESPAsyncWebServer request-owner allocator patch is absent"
    )
    request_text = request_source.read_text(encoding="utf-8")
    request_symbols = (
        "async_web_request_created",
        "async_web_request_destroyed",
        "async_web_request_owner_allocations",
        "async_web_request_owner_deallocations",
    )
    missing_source = [symbol for symbol in request_symbols if symbol not in request_text]
    assert not missing_source, "ESPAsyncWebServer request diagnostics missing: " + ", ".join(missing_source)

    request_objects = list((root / ".pio" / "build" / "wifi").glob("lib*/ESPAsyncWebServer/WebRequest.cpp.o"))
    assert len(request_objects) == 1, f"expected one compiled WebRequest object, found {len(request_objects)}"
    request_object = request_objects[0]
    assert request_object.stat().st_mtime_ns >= max(header.stat().st_mtime_ns, request_source.stat().st_mtime_ns), (
        "WebRequest object predates the patched ESPAsyncWebServer diagnostics source"
    )
    request_object_symbols = symbol_listing(nm, request_object)
    missing_object = [symbol for symbol in request_symbols if symbol not in request_object_symbols]
    assert not missing_object, "compiled WebRequest object is missing diagnostics: " + ", ".join(missing_object)

    response_source = source.parent / "WebResponses.cpp"
    response_text = response_source.read_text(encoding="utf-8")
    assert "FluidNC zero-byte response retry credit fix" in response_text, (
        "ESPAsyncWebServer chunked-response retry credit fix is absent from the dependency source"
    )
    response_tail = response_text.split("// execute sending whatever we have in sock buffs now", 1)[1].split(
        "if (_send_buffer_len == 0)", 1
    )[0]
    guarded_credit = response_tail.split("if (payloadlen) {", 1)[1].split("}", 1)[0]
    assert "_in_flight += payloadlen;" in guarded_credit and "--_in_flight_credit;" in guarded_credit, (
        "zero-byte response retries can still consume the final chunk in-flight credit"
    )
    response_objects = list((root / ".pio" / "build" / "wifi").glob("lib*/ESPAsyncWebServer/WebResponses.cpp.o"))
    assert len(response_objects) == 1, f"expected one compiled WebResponses object, found {len(response_objects)}"
    response_object = response_objects[0]
    assert response_object.stat().st_mtime_ns >= response_source.stat().st_mtime_ns, (
        "WebResponses object predates the patched chunked-response dependency source"
    )

    firmware = root / ".pio" / "build" / "wifi" / "firmware.elf"
    firmware_bin = root / ".pio" / "build" / "wifi" / "firmware.bin"
    assert firmware.stat().st_mtime_ns >= max(
        object_file.stat().st_mtime_ns,
        request_object.stat().st_mtime_ns,
        response_object.stat().st_mtime_ns,
    ), "firmware ELF predates a patched ESPAsyncWebServer dependency object"
    assert firmware_bin.stat().st_mtime_ns >= firmware.stat().st_mtime_ns, "firmware BIN predates firmware ELF"
    firmware_symbols = symbol_listing(nm, firmware)
    for symbol in (
        "async_web_resource_reject_abort_calls",
        "async_web_resource_reject_abort_max_us",
    ):
        assert symbol in firmware_symbols, f"firmware ELF is missing reject-abort diagnostics: {symbol}"


def verify_asynctcp_diagnostics(root: Path, packages: Path) -> None:
    override = (root / "platformio_override.ini").read_text(encoding="utf-8")
    patch_script = "pre:tools/patch_asynctcp_diagnostics.py"
    assert patch_script in override, f"wifi build does not enable {patch_script}"

    platformio = (root / "platformio.ini").read_text(encoding="utf-8")
    assert "ESP32Async/AsyncTCP@3.5.0" in platformio, "AsyncTCP dependency is not pinned to 3.5.0"
    assert "ESPAsyncWebServer.git#d009eff9ee94f92beccdf5812d89ec79aa44a6c1" in platformio, (
        "ESPAsyncWebServer dependency is not pinned to the reviewed commit"
    )

    source = root / ".pio" / "libdeps" / "wifi" / "AsyncTCP" / "src" / "AsyncTCP.cpp"
    assert source.is_file(), f"patched AsyncTCP source is missing: {source}"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        'extern "C" uint32_t async_tcp_event_queue_high_water()',
        'extern "C" uint32_t async_tcp_rx_timeouts()',
        'extern "C" void async_tcp_pcb_snapshot(',
        "async_rx_timeout_count.fetch_add",
        'extern "C" uint32_t async_tcp_accept_event_allocation_failures()',
        "_reset_tcp_callbacks(pcb, c);",
        "delete c;",
        'extern "C" uint32_t async_tcp_accept_admission_rejections()',
        'extern "C" uint32_t async_tcp_early_rst_accept_cleanups()',
        'extern "C" uint32_t async_tcp_accept_callbacks()',
        'extern "C" uint32_t async_tcp_accept_null_pcbs()',
        'extern "C" int32_t async_tcp_accept_last_null_pcb_error()',
        'extern "C" uint32_t async_tcp_accept_client_allocation_failures()',
        'extern "C" uint32_t async_tcp_accept_client_setup_failures()',
        'extern "C" uint32_t async_tcp_accept_pcb_active_time_wait_peak()',
        "async_accept_callbacks_count.fetch_add",
        "async_accept_null_pcb_count.fetch_add",
        "async_accept_last_null_pcb_error_value.store",
        "async_accept_client_allocation_failure_count.fetch_add",
        "async_accept_client_setup_failure_count.fetch_add",
        "snapshot->listen_backlog += pcb->backlog;",
        "snapshot->listen_accepts_pending += pcb->accepts_pending;",
        "static void _note_async_tcp_pcb_occupancy()",
        "for (tcp_pcb *pcb = tcp_active_pcbs; pcb; pcb = pcb->next)",
        "for (tcp_pcb *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next)",
        "async_tcp_pcb_peak_occupancy_counter.compare_exchange_weak",
        "_note_async_tcp_pcb_occupancy();",
        "bool removed_unpublished_accept = false",
        "_remove_events_for_client(client, &removed_unpublished_accept)",
        "async_accept_rst_before_dispatch_cleanups.fetch_add",
        "ASYNC_SERVER_MAX_CLIENTS = 8",
        "ASYNC_SERVER_PENDING_RESERVATION = 7 * 1024",
        "ASYNC_SERVER_FIRST_CLIENT_FLOOR = 24 * 1024",
        "ASYNC_SERVER_ADDITIONAL_CLIENT_FLOOR = 32 * 1024",
        "ASYNC_SERVER_LARGEST_BLOCK_FLOOR = 20 * 1024",
    )
    missing_source = [marker for marker in required_source if marker not in source_text]
    assert not missing_source, "AsyncTCP diagnostics source markers missing: " + ", ".join(missing_source)

    objects = list((root / ".pio" / "build" / "wifi").glob("lib*/AsyncTCP/AsyncTCP.cpp.o"))
    assert len(objects) == 1, f"expected one compiled AsyncTCP object, found {len(objects)}"
    object_file = objects[0]
    assert object_file.stat().st_mtime_ns >= source.stat().st_mtime_ns, (
        "AsyncTCP object predates the patched dependency source"
    )

    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    object_symbols = symbol_listing(nm, object_file)
    required_symbols = (
        "async_tcp_event_queue_high_water",
        "async_tcp_rx_timeouts",
        "async_tcp_last_rx_timeout_idle_ms",
        "async_tcp_pcb_snapshot",
        "async_tcp_accept_event_allocation_failures",
        "async_tcp_accept_admission_rejections",
        "async_tcp_early_rst_accept_cleanups",
        "async_tcp_accept_callbacks",
        "async_tcp_accept_null_pcbs",
        "async_tcp_accept_last_null_pcb_error",
        "async_tcp_accept_client_allocation_failures",
        "async_tcp_accept_client_setup_failures",
        "async_tcp_accept_pcb_active_time_wait_peak",
        "async_tcp_server_pending_accepts",
        "async_tcp_accept_last_effective_free",
    )
    missing_object = [symbol for symbol in required_symbols if symbol not in object_symbols]
    assert not missing_object, "compiled AsyncTCP object is missing diagnostics: " + ", ".join(missing_object)

    firmware = root / ".pio" / "build" / "wifi" / "firmware.elf"
    firmware_symbols = symbol_listing(nm, firmware)
    missing_firmware = [symbol for symbol in required_symbols if symbol not in firmware_symbols]
    assert not missing_firmware, "firmware ELF is missing AsyncTCP diagnostics: " + ", ".join(missing_firmware)


def verify_web_resource_hardening(root: Path, packages: Path) -> None:
    webui = root / "FluidNC" / "src" / "WebUI"
    channel_source = root / "FluidNC" / "src" / "Channel.cpp"
    channel_header = channel_source.with_suffix(".h")
    client_source = webui / "WebClient.cpp"
    client_header = webui / "WebClient.h"
    ws_channel_source = webui / "WSChannel.cpp"
    ws_channel_header = webui / "WSChannel.h"
    server_source = webui / "WebUIServer.cpp"
    policy_header = webui / "WebResourcePolicy.h"
    stats_source = root / "FluidNC" / "esp32" / "SysStats.cpp"
    async_ws_source = root / ".pio" / "libdeps" / "wifi" / "ESPAsyncWebServer" / "src" / "AsyncWebSocket.cpp"
    async_ws_header = async_ws_source.with_suffix(".h")

    policy_text = policy_header.read_text(encoding="utf-8")
    server_text = server_source.read_text(encoding="utf-8")
    stats_text = stats_source.read_text(encoding="utf-8")
    channel_text = channel_source.read_text(encoding="utf-8")
    channel_header_text = channel_header.read_text(encoding="utf-8")
    ws_channel_text = ws_channel_source.read_text(encoding="utf-8")
    async_ws_text = async_ws_source.read_text(encoding="utf-8")
    async_ws_header_text = async_ws_header.read_text(encoding="utf-8")
    assert "vSemaphoreDelete(_queue_mutex);" in channel_text, "Channel queue mutex destructor is missing"
    assert "virtual ~Channel();" in channel_header_text, "Channel destructor is not out-of-line"
    assert "SemaphoreHandle_t _queue_mutex = nullptr;" in channel_header_text, (
        "Channel queue mutex is allocated before potentially throwing members"
    )
    assert "xSemaphoreCreateMutex()" not in channel_header_text, (
        "Channel queue mutex allocation must occur after member construction"
    )
    assert "create_channel_queue_mutex_or_throw" in channel_text, "Channel queue mutex factory is missing"
    ws_write = ws_channel_text.split("size_t WSChannel::write(const uint8_t* buffer, size_t size)", 1)[1].split(
        "bool WSChannel::sendTXT", 1
    )[0]
    ws_send_text = ws_channel_text.split("bool WSChannel::sendTXT", 1)[1].split("WSChannel::~WSChannel", 1)[0]
    assert "get_client(" not in ws_write and "client->" not in ws_write, (
        "WebSocket binary output retains a raw library client beyond its lock"
    )
    assert "get_client(" not in ws_send_text and "client->" not in ws_send_text, (
        "WebSocket text output retains a raw library client beyond its lock"
    )
    assert "_server->queueLength(_clientNum, queue_length)" in ws_write
    assert "bool AsyncWebSocket::queueLength(uint32_t id, size_t &length)" in async_ws_text
    assert "bool queueLength(uint32_t id, size_t &length);" in async_ws_header_text
    required_policy_markers = (
        "first_file_stream_min_free_bytes  = 44u * 1024u",
        "heavy_http_reservation_bytes      = 12u * 1024u",
        "file_stream_admission(",
        "heavy_http_admission(",
        "is_heavy_http_command(",
    )
    missing_policy = [marker for marker in required_policy_markers if marker not in policy_text]
    assert not missing_policy, "web-resource policy markers missing: " + ", ".join(missing_policy)
    required_server_markers = (
        "HeavyHttpReservation reservation",
        "FileStreamReservation reservation",
        "active_heavy_http_responses * WebUI::ResourcePolicy::heavy_http_reservation_bytes",
        "executeCommandBackground",
        "schedule_deferred_webclient_kill",
    )
    missing_server = [marker for marker in required_server_markers if marker not in server_text]
    assert not missing_server, "web-resource server markers missing: " + ", ".join(missing_server)

    build = root / ".pio" / "build" / "wifi"
    client_object = build / "src" / "WebUI" / "WebClient.cpp.o"
    ws_channel_object = build / "src" / "WebUI" / "WSChannel.cpp.o"
    server_object = build / "src" / "WebUI" / "WebUIServer.cpp.o"
    stats_object = build / "esp32" / "SysStats.cpp.o"
    channel_object = build / "src" / "Channel.cpp.o"
    async_ws_objects = tuple(build.glob("lib*/ESPAsyncWebServer/AsyncWebSocket.cpp.o"))
    assert len(async_ws_objects) == 1, f"expected one compiled AsyncWebSocket object, found {len(async_ws_objects)}"
    async_ws_object = async_ws_objects[0]
    for object_file, sources, label in (
        (channel_object, (channel_source, channel_header), "Channel.cpp.o"),
        (client_object, (client_source, client_header), "WebClient.cpp.o"),
        (ws_channel_object, (ws_channel_source, ws_channel_header, async_ws_header), "WSChannel.cpp.o"),
        (async_ws_object, (async_ws_source, async_ws_header), "AsyncWebSocket.cpp.o"),
        (server_object, (server_source, policy_header, client_header), "WebUIServer.cpp.o"),
        (stats_object, (stats_source, policy_header), "SysStats.cpp.o"),
    ):
        assert object_file.is_file(), f"missing compiled web-resource object {label}: {object_file}"
        assert object_file.stat().st_mtime_ns >= max(source.stat().st_mtime_ns for source in sources), (
            f"{label} predates a web-resource source dependency"
        )

    required_objects = (channel_object, client_object, ws_channel_object, async_ws_object, server_object, stats_object)
    firmware = build / "firmware.elf"
    firmware_bin = build / "firmware.bin"
    assert firmware.is_file(), f"missing linked firmware ELF: {firmware}"
    assert firmware_bin.is_file(), f"missing linked firmware BIN: {firmware_bin}"
    assert firmware.stat().st_mtime_ns >= max(object_file.stat().st_mtime_ns for object_file in required_objects), (
        "firmware ELF predates a required web-resource object"
    )
    assert firmware_bin.stat().st_mtime_ns >= firmware.stat().st_mtime_ns, "firmware BIN predates firmware ELF"

    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    required_by_object = (
        (
            channel_object,
            (
                "Channel::~Channel()",
                "channel_queue_mutex_created",
                "channel_queue_mutex_destroyed",
            ),
        ),
        (
            client_object,
            (
                "WebUI::WebClient::executeCommandBackground(char const*)",
                "WebUI::WebClients::background_task(void*)",
                "WebUI::WebClient::~WebClient()",
            ),
        ),
        (
            ws_channel_object,
            (
                "WebUI::WSChannel::write(unsigned char const*, unsigned int)",
                "WebUI::WSChannel::sendTXT(std::basic_string_view<char, std::char_traits<char> >)",
            ),
        ),
        (
            async_ws_object,
            (
                "AsyncWebSocket::queueLength(unsigned int, unsigned int&)",
            ),
        ),
        (
            server_object,
            (
                "WebUI::ResourcePolicy::runtime_snapshot()",
                "WebUI::ResourcePolicy::is_heavy_http_command(char const*)",
            ),
        ),
        (
            stats_object,
            (
                "platform_sys_stats(JSONencoder&)",
                "platform_sys_stats(Channel&)",
            ),
        ),
    )
    required_firmware_symbols: list[str] = []
    for object_file, required_symbols in required_by_object:
        object_symbols = symbol_listing(nm, object_file)
        missing_object = [symbol for symbol in required_symbols if symbol not in object_symbols]
        assert not missing_object, f"{object_file.name} is missing web-resource symbols: " + ", ".join(missing_object)
        required_firmware_symbols.extend(required_symbols)

    firmware_symbols = symbol_listing(nm, firmware)
    missing_firmware = [symbol for symbol in required_firmware_symbols if symbol not in firmware_symbols]
    assert not missing_firmware, "firmware ELF is missing web-resource symbols: " + ", ".join(missing_firmware)

    telemetry_labels = (
        "Web heavy HTTP active",
        "Web file starts",
        "Web file completions",
        "Web heavy HTTP rejections",
        "Channel queue mutexes created",
        "Channel queue mutexes destroyed",
        "Async TCP accept callbacks",
        "Async TCP accept null PCBs",
        "Async TCP accept last null PCB error",
        "Async TCP accept client allocation failures",
        "Async TCP accept client setup failures",
        "TCP accept PCB active+TIME_WAIT peak",
        "Async WebSocket reject abort calls",
        "Async WebSocket reject abort max us",
        "TCP listener backlog",
        "TCP listener accepts pending",
    )
    missing_stats = [label for label in telemetry_labels if label not in stats_text]
    assert not missing_stats, "SysStats source is missing web-resource telemetry: " + ", ".join(missing_stats)
    strings = find_tool(packages, "xtensa-esp32-elf-strings.exe")
    firmware_strings = string_listing(strings, firmware)
    missing_labels = [label for label in telemetry_labels if label not in firmware_strings]
    assert not missing_labels, "firmware ELF is missing web-resource telemetry strings: " + ", ".join(missing_labels)


def verify_http_ota_resilience(root: Path, packages: Path) -> None:
    source = root / "FluidNC" / "src" / "WebUI" / "WebUIServer.cpp"
    source_text = source.read_text(encoding="utf-8")
    required_source = (
        '_webserver->on("/updatefw", HTTP_POST, handleUpdate, WebUpdateUpload)',
        "request->onDisconnect([request]()",
        "_firmware_upload_request == request",
        "firmware_ota_update_started.load",
        "Update.begin(maxSketchSpace, U_FLASH)",
        "Update.end(false)",
    )
    missing_source = [marker for marker in required_source if marker not in source_text]
    assert not missing_source, "HTTP OTA resilience source markers missing: " + ", ".join(missing_source)

    object_file = root / ".pio" / "build" / "wifi" / "src" / "WebUI" / "WebUIServer.cpp.o"
    assert object_file.is_file(), f"missing compiled WebUIServer object: {object_file}"
    assert object_file.stat().st_mtime_ns >= source.stat().st_mtime_ns, "WebUIServer object predates OTA resilience source"

    required_symbols = (
        "fluidnc_ota_active",
        "fluidnc_ota_expected_bytes",
        "fluidnc_ota_accepted_bytes",
        "fluidnc_ota_max_write_us",
        "fluidnc_ota_disconnect_aborts",
        "fluidnc_ota_failures",
        "fluidnc_ota_update_owned",
    )
    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    object_symbols = symbol_listing(nm, object_file)
    missing_object = [symbol for symbol in required_symbols if symbol not in object_symbols]
    assert not missing_object, "compiled WebUIServer object is missing OTA diagnostics: " + ", ".join(missing_object)

    firmware = root / ".pio" / "build" / "wifi" / "firmware.elf"
    firmware_symbols = symbol_listing(nm, firmware)
    missing_firmware = [symbol for symbol in required_symbols if symbol not in firmware_symbols]
    assert not missing_firmware, "firmware ELF is missing OTA diagnostics: " + ", ".join(missing_firmware)


def verify_output_url_runtime_integration(root: Path, packages: Path) -> None:
    provider = root / "FluidNC" / "src" / "WebUI" / "OutputUrlHttpProvider.cpp"
    policy = provider.with_name("OutputUrlHttpPolicy.h")
    coolant_source = root / "FluidNC" / "src" / "CoolantControl.cpp"
    coolant_header = coolant_source.with_suffix(".h")
    protocol_source = root / "FluidNC" / "src" / "Protocol.cpp"
    platformio = (root / "platformio.ini").read_text(encoding="utf-8")

    provider_text = provider.read_text(encoding="utf-8")
    policy_text = policy.read_text(encoding="utf-8")
    coolant_text = coolant_source.read_text(encoding="utf-8")
    protocol_text = protocol_source.read_text(encoding="utf-8")
    required_provider = (
        'extern "C" bool fluidnc_output_url_http_get',
        "provider_busy.test_and_set",
        "secure_phase_timeout_seconds(remaining)",
        "dns_gethostbyname",
        "setInsecure()",
        "catch (...)",
    )
    missing_provider = [marker for marker in required_provider if marker not in provider_text]
    assert not missing_provider, "output URL HTTP provider markers missing: " + ", ".join(missing_provider)
    assert "TaskHandle_t" not in provider_text and "xTaskNotifyGive" not in provider_text, (
        "output URL DNS callback retains a task-handle notification lifetime"
    )
    assert "HttpCommand" not in provider_text and "gc_state" not in provider_text and "config->_" not in provider_text, (
        "output URL provider is coupled to command/G-code/machine state"
    )
    assert '"GET %s HTTP/1.1\\r\\n"' in policy_text
    assert '"Connection: close\\r\\n"' in policy_text
    assert "StopGuard" in policy_text and "perform_get_noexcept" in policy_text
    assert "-Wl,-u,fluidnc_output_url_http_get" in platformio, (
        "WiFi link does not retain the strong provider behind the feature adapter's weak port"
    )

    assert "void CoolantControl::stop_and_notify()" in coolant_text
    assert "notify_output_url_transition(OutputUrlFlood, false)" in coolant_text
    assert "notify_output_url_transition(OutputUrlMist, false)" in coolant_text
    assert "void stop_and_notify();" in coolant_header.read_text(encoding="utf-8")
    assert "config->_coolant->stop_and_notify();" in protocol_text

    build = root / ".pio" / "build" / "wifi"
    provider_object = build / "src" / "WebUI" / "OutputUrlHttpProvider.cpp.o"
    coolant_object = build / "src" / "CoolantControl.cpp.o"
    protocol_object = build / "src" / "Protocol.cpp.o"
    for object_file, sources, label in (
        (provider_object, (provider, policy), "output URL provider"),
        (coolant_object, (coolant_source, coolant_header), "coolant transition hook"),
        (protocol_object, (protocol_source,), "late-reset hook"),
    ):
        assert object_file.is_file(), f"missing compiled {label} object: {object_file}"
        assert object_file.stat().st_mtime_ns >= max(source.stat().st_mtime_ns for source in sources), (
            f"compiled {label} object predates its source"
        )

    nm = find_tool(packages, "xtensa-esp32-elf-nm.exe")
    provider_symbols = symbol_listing(nm, provider_object)
    assert "fluidnc_output_url_http_get" in provider_symbols, "provider object is missing the strong C ABI"
    coolant_symbols = symbol_listing(nm, coolant_object)
    assert "CoolantControl::stop_and_notify()" in coolant_symbols, "coolant object is missing task-only stop hook"

    firmware = build / "firmware.elf"
    firmware_symbols = symbol_listing(nm, firmware)
    assert "fluidnc_output_url_http_get" in firmware_symbols, (
        "firmware ELF garbage-collected the strong provider behind the weak feature port"
    )
    assert "CoolantControl::stop_and_notify()" in firmware_symbols, "firmware ELF is missing task-only coolant stop hook"


def verify_ota_partition_fit(root: Path) -> None:
    partition_file = root / "min_littlefs.csv"
    firmware = root / ".pio" / "build" / "wifi" / "firmware.bin"
    assert partition_file.is_file(), f"partition table is missing: {partition_file}"
    assert firmware.is_file(), f"OTA firmware image is missing: {firmware}"

    app_partition_size: int | None = None
    with partition_file.open(newline="", encoding="utf-8") as stream:
        rows = csv.reader(line for line in stream if not line.lstrip().startswith("#"))
        for row in rows:
            if len(row) >= 5 and row[0].strip() == "app0":
                app_partition_size = int(row[4].strip(), 0)
                break

    assert app_partition_size is not None, f"app0 partition is missing from {partition_file}"
    assert firmware.stat().st_size <= app_partition_size, (
        f"firmware image ({firmware.stat().st_size} bytes) exceeds "
        f"app0 OTA partition ({app_partition_size} bytes)"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument(
        "--packages",
        type=Path,
        default=default_platformio_packages(),
    )
    args = parser.parse_args()

    try:
        verify_clean_build_receipt(args.root.resolve(), args.packages.resolve())
        verify_dependency_resolution(
            args.root.resolve(),
            args.packages.resolve().parent,
        )
        verify_watchdog_calls(args.root.resolve(), args.packages.resolve())
        verify_recovery_hooks(args.root.resolve(), args.packages.resolve())
        verify_live_soak_identity(args.root.resolve(), args.packages.resolve())
        verify_supervisor_uses_ordered_timing_sample(args.root.resolve(), args.packages.resolve())
        verify_localfs_uses_no_autoformat_policy(args.root.resolve(), args.packages.resolve())
        verify_retained_backtrace(args.root.resolve())
        verify_retryable_websocket_close(args.root.resolve(), args.packages.resolve())
        verify_asynctcp_diagnostics(args.root.resolve(), args.packages.resolve())
        verify_web_resource_hardening(args.root.resolve(), args.packages.resolve())
        verify_http_ota_resilience(args.root.resolve(), args.packages.resolve())
        verify_output_url_runtime_integration(args.root.resolve(), args.packages.resolve())
        verify_ota_partition_fit(args.root.resolve())
    except (AssertionError, subprocess.CalledProcessError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    print("PASS: watchdog, recovery hooks, retryable WebSocket close, artifact-bound web-resource hardening, reproducible HTTP/AsyncTCP diagnostics, resilient HTTP OTA, bounded output URL runtime hooks, retained backtrace, and OTA partition fit verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
