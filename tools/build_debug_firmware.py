#!/usr/bin/env python3
"""Create a WiFi debug-firmware artifact with a bound PlatformIO-core receipt.

The shared PlatformIO cache is mutable across unrelated worktrees.  This
wrapper deliberately requires an explicit, isolated core directory, cleans the
target before compiling, and records the exact artifact and source-input
identities that the verifier must later read back.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ENVIRONMENT = "wifi"
BUILD_DIR = Path(".pio") / "build" / ENVIRONMENT
INPUT_MANIFEST_NAME = "firmware-build-inputs.json"
RECEIPT_NAME = "firmware-build-receipt.json"
SCHEMA_VERSION = 1
MANIFEST_IDENTITY_FIELDS = (
    "inputFileCount",
    "inputAggregateSha256",
    "gitlinkCount",
    "gitlinkAggregateSha256",
    "overlayFileCount",
    "overlayAggregateSha256",
)
PLATFORMIO_RUNTIME_PROBE = (
    "import importlib.metadata,json,platformio,sys; from pathlib import Path; "
    "dist=importlib.metadata.distribution('platformio'); "
    "print(json.dumps({'pythonPath':str(Path(sys.executable).resolve()),"
    "'pythonVersion':sys.version,'platformioVersion':importlib.metadata.version('platformio'),"
    "'packagePath':str(Path(platformio.__file__).resolve().parent),"
    "'distInfoPath':str(Path(dist._path).resolve())},sort_keys=True))"
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def directory_aggregate(path: Path) -> tuple[int, str]:
    if not path.is_dir():
        raise RuntimeError(f"runtime package directory is missing: {path}")
    files = sorted(
        (
            candidate
            for candidate in path.rglob("*")
            if candidate.is_file()
            and "__pycache__" not in candidate.relative_to(path).parts
            and candidate.suffix != ".pyc"
        ),
        key=lambda candidate: candidate.relative_to(path).as_posix(),
    )
    digest = hashlib.sha256()
    for candidate in files:
        digest.update(candidate.relative_to(path).as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(candidate.stat().st_size).encode("ascii"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(sha256_file(candidate)))
        digest.update(b"\n")
    return len(files), digest.hexdigest().upper()


def platformio_runtime_identity(executable: str) -> dict[str, str | int]:
    candidate = Path(executable)
    resolved = candidate.resolve() if candidate.is_file() else None
    if resolved is None:
        found = shutil.which(executable)
        if not found:
            raise RuntimeError(f"PlatformIO executable was not found: {executable}")
        resolved = Path(found).resolve()
    runtime_python = resolved.parent / "python.exe"
    if not runtime_python.is_file():
        raise RuntimeError(f"PlatformIO launcher lacks its sibling Python interpreter: {resolved}")
    result = subprocess.run(
        [str(runtime_python), "-c", PLATFORMIO_RUNTIME_PROBE], check=True, capture_output=True, text=True
    )
    try:
        probe = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"PlatformIO runtime probe returned invalid JSON: {resolved}: {error}") from error
    if not isinstance(probe, dict) or probe.get("pythonPath") != str(runtime_python.resolve()):
        raise RuntimeError(f"PlatformIO runtime probe did not use the expected interpreter: {resolved}")
    package = Path(str(probe.get("packagePath", ""))).resolve()
    dist_info = Path(str(probe.get("distInfoPath", ""))).resolve()
    package_files, package_aggregate = directory_aggregate(package)
    dist_files, dist_aggregate = directory_aggregate(dist_info)
    return {
        "launcherPath": str(resolved),
        "launcherSha256": sha256_file(resolved),
        "pythonPath": str(runtime_python.resolve()),
        "pythonSha256": sha256_file(runtime_python),
        "pythonVersion": str(probe.get("pythonVersion", "")),
        "platformioVersion": str(probe.get("platformioVersion", "")),
        "packagePath": str(package),
        "packageFileCount": package_files,
        "packageAggregateSha256": package_aggregate,
        "distInfoPath": str(dist_info),
        "distInfoFileCount": dist_files,
        "distInfoAggregateSha256": dist_aggregate,
    }


def git_stdout(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args], check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def require_clean_worktree(root: Path) -> None:
    status = git_stdout(root, "status", "--porcelain", "--untracked-files=all")
    if status:
        raise RuntimeError(f"source worktree is not clean: {status}")


def read_json(path: Path, label: str) -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"{label} is missing: {path}")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{label} is invalid: {path}: {error}") from error
    if not isinstance(payload, dict):
        raise RuntimeError(f"{label} must be a JSON object: {path}")
    return payload


def input_manifest_identity(manifest: dict[str, Any]) -> dict[str, Any]:
    git = manifest.get("git")
    if not isinstance(git, dict) or not isinstance(git.get("commit"), str):
        raise RuntimeError("firmware build-input manifest lacks a Git commit")
    identity = {"sourceGitCommit": git["commit"]}
    for field in MANIFEST_IDENTITY_FIELDS:
        value = manifest.get(field)
        if value is None:
            raise RuntimeError(f"firmware build-input manifest lacks {field}")
        identity[field] = value
    if not isinstance(identity["inputAggregateSha256"], str) or len(identity["inputAggregateSha256"]) != 64:
        raise RuntimeError("firmware build-input manifest lacks its input aggregate hash")
    return identity


def build_input_manifest(root: Path) -> dict[str, Any]:
    tool_path = root / "tools" / "manifest_firmware_build_inputs.py"
    if not tool_path.is_file():
        raise RuntimeError(f"input-manifest tool is missing: {tool_path}")
    spec = importlib.util.spec_from_file_location("firmware_build_inputs", tool_path)
    module = importlib.util.module_from_spec(spec)
    if spec.loader is None:
        raise RuntimeError(f"could not load input-manifest tool: {tool_path}")
    spec.loader.exec_module(module)
    manifest = module.build_manifest(root)
    if not isinstance(manifest, dict):
        raise RuntimeError("input-manifest tool returned an invalid manifest")
    input_manifest_identity(manifest)
    return manifest


def create_build_receipt(
    *,
    root: Path,
    core_dir: Path,
    environment: str,
    firmware_bin: Path,
    firmware_elf: Path,
    input_manifest: Path,
    prebuild_input_identity: dict[str, Any],
    platformio_runtime: dict[str, str | int],
) -> dict[str, Any]:
    root = root.resolve()
    core_dir = core_dir.resolve()
    firmware_bin = firmware_bin.resolve()
    firmware_elf = firmware_elf.resolve()
    input_manifest = input_manifest.resolve()
    if environment != ENVIRONMENT:
        raise RuntimeError(f"unsupported debug environment: {environment}")
    packages_dir = core_dir / "packages"
    if not packages_dir.is_dir():
        raise RuntimeError(f"isolated PlatformIO packages are missing: {packages_dir}")
    if not firmware_bin.is_file() or not firmware_elf.is_file():
        raise RuntimeError("clean build did not produce both firmware.bin and firmware.elf")

    manifest = read_json(input_manifest, "firmware build-input manifest")
    manifest_identity = input_manifest_identity(manifest)
    if prebuild_input_identity != manifest_identity:
        raise RuntimeError("source input identity changed while the firmware was building")

    builder = root / "tools" / Path(__file__).name
    manifest_tool = root / "tools" / "manifest_firmware_build_inputs.py"
    if not builder.is_file() or not manifest_tool.is_file():
        raise RuntimeError("receipt tools are missing from the source worktree")
    if set(platformio_runtime) != {
        "launcherPath",
        "launcherSha256",
        "pythonPath",
        "pythonSha256",
        "pythonVersion",
        "platformioVersion",
        "packagePath",
        "packageFileCount",
        "packageAggregateSha256",
        "distInfoPath",
        "distInfoFileCount",
        "distInfoAggregateSha256",
    }:
        raise RuntimeError("PlatformIO executable identity is incomplete")

    return {
        "schemaVersion": SCHEMA_VERSION,
        "environment": environment,
        "sourceGitCommit": git_stdout(root, "rev-parse", "HEAD"),
        "platformioCoreDir": str(core_dir),
        "packagesDir": str(packages_dir.resolve()),
        "platformioRuntime": platformio_runtime,
        "firmwareBinSha256": sha256_file(firmware_bin),
        "firmwareElfSha256": sha256_file(firmware_elf),
        "inputManifestSha256": sha256_file(input_manifest),
        "inputManifestInputAggregateSha256": manifest_identity["inputAggregateSha256"],
        "inputManifestGitCommit": manifest_identity["sourceGitCommit"],
        "preBuildInputIdentity": prebuild_input_identity,
        "builderScriptSha256": sha256_file(builder),
        "inputManifestToolSha256": sha256_file(manifest_tool),
    }


def write_creation_only_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)
        stream.write("\n")


def run_clean_build(root: Path, core_dir: Path, platformio: str) -> Path:
    root = root.resolve()
    core_dir = core_dir.resolve()
    core_dir.mkdir(parents=True, exist_ok=True)
    if not core_dir.is_dir():
        raise RuntimeError(f"--core-dir is not a directory: {core_dir}")
    require_clean_worktree(root)
    prebuild_manifest = build_input_manifest(root)
    prebuild_input_identity = input_manifest_identity(prebuild_manifest)
    platformio_runtime = platformio_runtime_identity(platformio)

    environment = {
        key: value for key, value in os.environ.items() if not key.upper().startswith("PLATFORMIO_")
    }
    environment["PLATFORMIO_CORE_DIR"] = str(core_dir)
    command = [str(platformio_runtime["pythonPath"]), "-m", "platformio", "run", "-e", ENVIRONMENT]
    subprocess.run([*command, "-t", "clean"], cwd=root, env=environment, check=True)
    output = root / BUILD_DIR
    stale_outputs = [
        path
        for path in (output / "firmware.bin", output / "firmware.elf", output / INPUT_MANIFEST_NAME, output / RECEIPT_NAME)
        if path.exists()
    ]
    if stale_outputs:
        raise RuntimeError("clean build left expected output artifacts behind: " + ", ".join(map(str, stale_outputs)))
    subprocess.run(command, cwd=root, env=environment, check=True)
    require_clean_worktree(root)

    input_manifest = output / INPUT_MANIFEST_NAME
    receipt_path = output / RECEIPT_NAME
    postbuild_manifest = build_input_manifest(root)
    postbuild_input_identity = input_manifest_identity(postbuild_manifest)
    if prebuild_input_identity != postbuild_input_identity:
        raise RuntimeError("source input identity changed while the firmware was building")
    if platformio_runtime_identity(str(platformio_runtime["launcherPath"])) != platformio_runtime:
        raise RuntimeError("PlatformIO runtime identity changed while the firmware was building")
    write_creation_only_json(input_manifest, postbuild_manifest)
    receipt = create_build_receipt(
        root=root,
        core_dir=core_dir,
        environment=ENVIRONMENT,
        firmware_bin=output / "firmware.bin",
        firmware_elf=output / "firmware.elf",
        input_manifest=input_manifest,
        prebuild_input_identity=prebuild_input_identity,
        platformio_runtime=platformio_runtime,
    )
    write_creation_only_json(receipt_path, receipt)
    return receipt_path


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--core-dir", type=Path, required=True)
    parser.add_argument("--platformio", default="platformio")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        receipt = run_clean_build(args.root, args.core_dir, args.platformio)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"PASS: clean isolated build receipt written to {receipt}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
