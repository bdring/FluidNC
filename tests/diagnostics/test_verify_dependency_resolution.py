import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


VERIFIER = Path(__file__).with_name("verify_debug_firmware.py")
BUILD_TOOL = Path(__file__).parents[2] / "tools" / "build_debug_firmware.py"
MANIFEST_TOOL = Path(__file__).parents[2] / "tools" / "manifest_firmware_build_inputs.py"


def load_verifier():
    spec = importlib.util.spec_from_file_location("verify_debug_firmware", VERIFIER)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def load_build_tool():
    spec = importlib.util.spec_from_file_location("build_debug_firmware", BUILD_TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def init_git_repo(path):
    path.mkdir(parents=True)
    subprocess.run(["git", "init", "-b", "main", str(path)], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(path), "config", "user.name", "Test"], check=True)
    subprocess.run(["git", "-C", str(path), "config", "user.email", "test@example.invalid"], check=True)
    (path / "source.cpp").write_text("fixture\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(path), "add", "."], check=True)
    subprocess.run(["git", "-C", str(path), "commit", "-m", "fixture"], check=True, capture_output=True)
    return subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload), encoding="utf-8")


def dependency_fixture(tmp_path):
    root = tmp_path / "source"
    home = tmp_path / "platformio"
    root.mkdir(parents=True)
    platform_commit = init_git_repo(home / "platforms/espressif32")
    json_commit = init_git_repo(root / ".pio/libdeps/wifi/JsonStreamingParser")
    web_commit = init_git_repo(root / ".pio/libdeps/wifi/ESPAsyncWebServer")
    write_json(
        home / "packages/framework-arduinoespressif32/package.json",
        {"version": "framework-version"},
    )
    (home / "packages/framework-arduinoespressif32/source.h").write_text("framework\n", encoding="utf-8")
    write_json(home / "packages/tool-esptoolpy/package.json", {"version": "esptool-version"})
    write_json(home / "packages/toolchain-xtensa-esp32/package.json", {"version": "toolchain-version"})
    for name, version in (
        ("AsyncTCP", "async-version"),
        ("TMCStepper", "tmc-version"),
        ("ESP8266 and ESP32 OLED driver for SSD1306 displays", "oled-version"),
    ):
        directory = root / f".pio/libdeps/wifi/{name}"
        write_json(directory / ".piopm", {"name": name, "version": version})
        (directory / "source.cpp").write_text(f"{name}\n", encoding="utf-8")
    expected = {
        "platformCommit": platform_commit,
        "frameworkVersion": "framework-version",
        "AsyncTCP": "async-version",
        "TMCStepper": "tmc-version",
        "OLED": "oled-version",
        "JsonStreamingParserCommit": json_commit,
        "ESPAsyncWebServerCommit": web_commit,
        "tool-esptoolpy": "esptool-version",
        "toolchain-xtensa-esp32": "toolchain-version",
    }
    return root, home, expected


def bind_content_expectations(verifier, root, home, expected):
    roots = {
        "platformContentSha256": home / "platforms/espressif32",
        "frameworkContentSha256": home / "packages/framework-arduinoespressif32",
        "AsyncTCPContentSha256": root / ".pio/libdeps/wifi/AsyncTCP",
        "TMCStepperContentSha256": root / ".pio/libdeps/wifi/TMCStepper",
        "OLEDContentSha256": root / ".pio/libdeps/wifi/ESP8266 and ESP32 OLED driver for SSD1306 displays",
        "JsonStreamingParserContentSha256": root / ".pio/libdeps/wifi/JsonStreamingParser",
        "ESPAsyncWebServerContentSha256": root / ".pio/libdeps/wifi/ESPAsyncWebServer",
    }
    for key, directory in roots.items():
        expected[key] = verifier.directory_aggregate(directory)[1]


def test_default_packages_prefers_the_platformio_core_dir_environment(monkeypatch, tmp_path):
    verifier = load_verifier()
    isolated_core = tmp_path / "isolated-platformio"

    monkeypatch.setenv("PLATFORMIO_CORE_DIR", str(isolated_core))
    assert verifier.default_platformio_packages() == isolated_core / "packages"

    monkeypatch.delenv("PLATFORMIO_CORE_DIR")
    assert verifier.default_platformio_packages() == Path.home() / ".platformio" / "packages"


def test_clean_build_receipt_binds_artifacts_to_the_selected_core(tmp_path):
    verifier = load_verifier()
    builder = load_build_tool()
    root = tmp_path / "source"
    init_git_repo(root)
    subprocess.run(["git", "-C", str(root), "remote", "add", "origin", "https://example.invalid/FluidNC.git"], check=True)
    (root / "tools").mkdir()
    (root / ".gitignore").write_text(".pio/\n*.pyc\nFluidNC/src/ignored.cpp\n", encoding="utf-8")
    shutil.copy2(BUILD_TOOL, root / "tools" / BUILD_TOOL.name)
    shutil.copy2(MANIFEST_TOOL, root / "tools" / MANIFEST_TOOL.name)
    ignored_input = root / "FluidNC" / "src" / "ignored.cpp"
    ignored_input.parent.mkdir(parents=True)
    ignored_input.write_text("baseline\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(root), "add", ".gitignore", "tools"], check=True)
    subprocess.run(["git", "-C", str(root), "commit", "-m", "tools"], check=True, capture_output=True)
    commit = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()

    core = tmp_path / "isolated-core"
    packages = core / "packages"
    packages.mkdir(parents=True)
    output = root / ".pio" / "build" / "wifi"
    output.mkdir(parents=True)
    firmware_bin = output / "firmware.bin"
    firmware_elf = output / "firmware.elf"
    firmware_bin.write_bytes(b"bin")
    firmware_elf.write_bytes(b"elf")
    input_manifest = output / "firmware-build-inputs.json"
    manifest = builder.build_input_manifest(root)
    input_identity = builder.input_manifest_identity(manifest)
    assert input_identity["sourceGitCommit"] == commit
    input_manifest.write_text(json.dumps(manifest), encoding="utf-8")
    platformio_runtime = {
        "launcherPath": "fixture-launcher",
        "launcherSha256": "D" * 64,
        "pythonPath": "fixture-python",
        "pythonSha256": "E" * 64,
        "pythonVersion": "fixture-python",
        "platformioVersion": "fixture-platformio",
        "packagePath": "fixture-package",
        "packageFileCount": 1,
        "packageAggregateSha256": "F" * 64,
        "distInfoPath": "fixture-dist",
        "distInfoFileCount": 1,
        "distInfoAggregateSha256": "0" * 64,
    }
    verifier_platformio_runtime = dict(platformio_runtime)
    monkeypatch = pytest.MonkeyPatch()
    monkeypatch.setattr(verifier, "platformio_runtime_identity", lambda _path: verifier_platformio_runtime)
    monkeypatch.setattr(verifier, "EXPECTED_PLATFORMIO_RUNTIME", {
        key: value for key, value in verifier_platformio_runtime.items() if key not in {"launcherPath", "launcherSha256", "pythonPath", "packagePath", "distInfoPath"}
    })

    with pytest.raises(AssertionError, match="build receipt"):
        verifier.verify_clean_build_receipt(root, packages)

    receipt = builder.create_build_receipt(
        root=root,
        core_dir=core,
        environment="wifi",
        firmware_bin=firmware_bin,
        firmware_elf=firmware_elf,
        input_manifest=input_manifest,
        prebuild_input_identity=input_identity,
        platformio_runtime=platformio_runtime,
    )
    builder.write_creation_only_json(output / "firmware-build-receipt.json", receipt)
    verifier.verify_clean_build_receipt(root, packages)

    firmware_bin.write_bytes(b"different")
    with pytest.raises(AssertionError, match="firmware.bin hash"):
        verifier.verify_clean_build_receipt(root, packages)

    firmware_bin.write_bytes(b"bin")
    receipt_path = output / "firmware-build-receipt.json"
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    receipt["firmwareBinSha256"] = builder.sha256_file(firmware_bin)
    receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
    ignored_input.write_text("changed\n", encoding="utf-8")
    with pytest.raises(AssertionError, match="source inputs differ"):
        verifier.verify_clean_build_receipt(root, packages)
    monkeypatch.undo()


def test_clean_build_wrapper_cleans_and_rebuilds_under_the_selected_core(monkeypatch, tmp_path):
    builder = load_build_tool()
    root = tmp_path / "source"
    core = tmp_path / "isolated-core"
    (root / "tools").mkdir(parents=True)
    (root / "tools" / "manifest_firmware_build_inputs.py").write_text("# fixture\n", encoding="utf-8")
    (core / "packages").mkdir(parents=True)
    calls = []

    def fake_run(command, *, cwd, env, check):
        calls.append((command, cwd, env, check))
        output = root / ".pio" / "build" / "wifi"
        if command == ["pio", "run", "-e", "wifi"]:
            output.mkdir(parents=True, exist_ok=True)
            (output / "firmware.bin").write_bytes(b"bin")
            (output / "firmware.elf").write_bytes(b"elf")
        return subprocess.CompletedProcess(command, 0)

    monkeypatch.setattr(builder, "require_clean_worktree", lambda _root: None)
    monkeypatch.setattr(builder.subprocess, "run", fake_run)
    monkeypatch.setattr(builder, "create_build_receipt", lambda **_kwargs: {"schemaVersion": 1})
    fixture_runtime = {
        "launcherPath": "pio",
        "launcherSha256": "D" * 64,
        "pythonPath": "fixture-python",
        "pythonSha256": "E" * 64,
        "pythonVersion": "fixture-python",
        "platformioVersion": "fixture-platformio",
        "packagePath": "fixture-package",
        "packageFileCount": 1,
        "packageAggregateSha256": "F" * 64,
        "distInfoPath": "fixture-dist",
        "distInfoFileCount": 1,
        "distInfoAggregateSha256": "0" * 64,
    }
    monkeypatch.setattr(builder, "platformio_runtime_identity", lambda path: fixture_runtime)
    monkeypatch.setenv("PLATFORMIO_PACKAGES_DIR", str(tmp_path / "foreign-packages"))
    monkeypatch.setenv("PLATFORMIO_PLATFORMS_DIR", str(tmp_path / "foreign-platforms"))
    monkeypatch.setenv("PLATFORMIO_BUILD_DIR", str(tmp_path / "foreign-build"))
    monkeypatch.setenv("PLATFORMIO_LIBDEPS_DIR", str(tmp_path / "foreign-libdeps"))
    manifest = {
        "git": {"commit": "f" * 40},
        "inputFileCount": 1,
        "inputAggregateSha256": "A" * 64,
        "gitlinkCount": 0,
        "gitlinkAggregateSha256": "B" * 64,
        "overlayFileCount": 0,
        "overlayAggregateSha256": "C" * 64,
    }
    monkeypatch.setattr(builder, "build_input_manifest", lambda _root: manifest)

    receipt = builder.run_clean_build(root, core, "pio")

    assert receipt == root / ".pio" / "build" / "wifi" / "firmware-build-receipt.json"
    assert receipt.is_file()
    assert [call[0] for call in calls[:2]] == [
        ["fixture-python", "-m", "platformio", "run", "-e", "wifi", "-t", "clean"],
        ["fixture-python", "-m", "platformio", "run", "-e", "wifi"],
    ]
    assert all(call[2]["PLATFORMIO_CORE_DIR"] == str(core.resolve()) for call in calls)
    for key in (
        "PLATFORMIO_PACKAGES_DIR",
        "PLATFORMIO_PLATFORMS_DIR",
        "PLATFORMIO_BUILD_DIR",
        "PLATFORMIO_LIBDEPS_DIR",
    ):
        assert all(key not in call[2] for call in calls)


def test_clean_build_wrapper_rejects_a_source_identity_change_during_build(monkeypatch, tmp_path):
    builder = load_build_tool()
    root = tmp_path / "source"
    core = tmp_path / "isolated-core"
    (root / "tools").mkdir(parents=True)
    (root / "tools" / "manifest_firmware_build_inputs.py").write_text("# fixture\n", encoding="utf-8")
    (core / "packages").mkdir(parents=True)
    output = root / ".pio" / "build" / "wifi"

    def fake_run(command, *, cwd, env, check):
        if command == ["pio", "run", "-e", "wifi"]:
            output.mkdir(parents=True, exist_ok=True)
            (output / "firmware.bin").write_bytes(b"bin")
            (output / "firmware.elf").write_bytes(b"elf")
        return subprocess.CompletedProcess(command, 0)

    baseline = {
        "git": {"commit": "a" * 40},
        "inputFileCount": 1,
        "inputAggregateSha256": "A" * 64,
        "gitlinkCount": 0,
        "gitlinkAggregateSha256": "B" * 64,
        "overlayFileCount": 0,
        "overlayAggregateSha256": "C" * 64,
    }
    changed = {**baseline, "git": {"commit": "b" * 40}}
    manifests = iter((baseline, changed))
    monkeypatch.setattr(builder, "require_clean_worktree", lambda _root: None)
    monkeypatch.setattr(builder.subprocess, "run", fake_run)
    monkeypatch.setattr(builder, "build_input_manifest", lambda _root: next(manifests))
    fixture_runtime = {
        "launcherPath": "pio",
        "launcherSha256": "D" * 64,
        "pythonPath": "fixture-python",
        "pythonSha256": "E" * 64,
        "pythonVersion": "fixture-python",
        "platformioVersion": "fixture-platformio",
        "packagePath": "fixture-package",
        "packageFileCount": 1,
        "packageAggregateSha256": "F" * 64,
        "distInfoPath": "fixture-dist",
        "distInfoFileCount": 1,
        "distInfoAggregateSha256": "0" * 64,
    }
    monkeypatch.setattr(builder, "platformio_runtime_identity", lambda path: fixture_runtime)

    with pytest.raises(RuntimeError, match="source input identity changed"):
        builder.run_clean_build(root, core, "pio")


def test_dependency_resolution_accepts_only_the_bound_versions(tmp_path):
    verifier = load_verifier()
    root, home, expected = dependency_fixture(tmp_path)
    bind_content_expectations(verifier, root, home, expected)

    verifier.verify_dependency_resolution(root, home, expected)

    metadata = root / ".pio/libdeps/wifi/AsyncTCP/.piopm"
    write_json(metadata, {"name": "AsyncTCP", "version": "unexpected"})
    with pytest.raises(AssertionError, match="AsyncTCP"):
        verifier.verify_dependency_resolution(root, home, expected)


def test_dependency_resolution_rejects_dirty_git_or_changed_package_content(tmp_path):
    verifier = load_verifier()
    root, home, expected = dependency_fixture(tmp_path)
    bind_content_expectations(verifier, root, home, expected)
    json_source = root / ".pio/libdeps/wifi/JsonStreamingParser/source.cpp"
    json_source.write_text("dirty\n", encoding="utf-8")

    with pytest.raises(AssertionError, match="dirty"):
        verifier.verify_dependency_resolution(root, home, expected)

    root2, home2, expected2 = dependency_fixture(tmp_path / "second")
    bind_content_expectations(verifier, root2, home2, expected2)
    tmc_source = root2 / ".pio/libdeps/wifi/TMCStepper/source.cpp"
    tmc_source.write_text("changed with same version\n", encoding="utf-8")
    with pytest.raises(AssertionError, match="TMCStepper content aggregate"):
        verifier.verify_dependency_resolution(root2, home2, expected2)


def test_dependency_resolution_rejects_toolchain_version_drift(tmp_path):
    verifier = load_verifier()
    root, home, expected = dependency_fixture(tmp_path)
    bind_content_expectations(verifier, root, home, expected)
    write_json(home / "packages/toolchain-xtensa-esp32/package.json", {"version": "unexpected"})

    with pytest.raises(AssertionError, match="toolchain-xtensa-esp32"):
        verifier.verify_dependency_resolution(root, home, expected)
