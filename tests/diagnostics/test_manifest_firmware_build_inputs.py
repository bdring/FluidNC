import importlib.util
import subprocess
from pathlib import Path

import pytest


TOOL = Path(__file__).parents[2] / "tools" / "manifest_firmware_build_inputs.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("manifest_firmware_build_inputs", TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_discovery_includes_firmware_build_and_patcher_inputs_only(tmp_path):
    tool = load_tool()
    included = [
        "platformio.ini",
        "platformio_override.ini",
        "git-version.py",
        "min_littlefs.csv",
        "FluidNC/src/new.cpp",
        "FluidNC/esp32/new.cpp",
        "FluidNC/include/new.h",
        "FluidNC/ld/esp32/new.py",
        "FluidNC/stdfs/new.cpp",
        "lib/websocket_compat.h",
        "libraries/local/library.json",
        "tools/patch_asynctcp_diagnostics.py",
    ]
    excluded = [
        "docs/note.md",
        "tests/diagnostics/test_other.py",
        "tools/compare_esp32_runtime_images.py",
        ".pio/build/wifi/firmware.bin",
    ]
    for relative in included + excluded:
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(relative, encoding="utf-8")

    discovered = {path.relative_to(tmp_path).as_posix() for path in tool.discover_inputs(tmp_path)}

    assert discovered == set(included)


def test_aggregate_is_path_order_independent_and_content_sensitive():
    tool = load_tool()
    first = [
        {"path": "b", "bytes": 2, "sha256": "BB" * 32},
        {"path": "a", "bytes": 1, "sha256": "AA" * 32},
    ]
    reordered = list(reversed(first))
    changed = [dict(entry) for entry in first]
    changed[0]["sha256"] = "CC" * 32

    assert tool.aggregate_entries(first) == tool.aggregate_entries(reordered)
    assert tool.aggregate_entries(first) != tool.aggregate_entries(changed)


def init_git_fixture(root):
    subprocess.run(["git", "init", "-b", "fixture", str(root)], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(root), "config", "user.name", "Test"], check=True)
    subprocess.run(["git", "-C", str(root), "config", "user.email", "test@example.invalid"], check=True)
    subprocess.run(
        ["git", "-C", str(root), "remote", "add", "origin", "https://example.invalid/FluidNC.git"], check=True
    )
    for relative in ["platformio.ini", "FluidNC/src/base.cpp", "lib/websocket_compat.h"]:
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("baseline\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(root), "add", "."], check=True)
    subprocess.run(["git", "-C", str(root), "commit", "-m", "fixture"], check=True, capture_output=True)


def test_build_manifest_binds_git_metadata_and_modified_untracked_deleted_overlay(tmp_path):
    tool = load_tool()
    init_git_fixture(tmp_path)
    (tmp_path / "FluidNC/src/base.cpp").write_text("modified\n", encoding="utf-8")
    (tmp_path / "FluidNC/src/new.cpp").write_text("untracked\n", encoding="utf-8")
    (tmp_path / "lib/websocket_compat.h").unlink()

    manifest = tool.build_manifest(tmp_path)

    assert manifest["git"]["remoteOrigin"] == "https://example.invalid/FluidNC.git"
    assert manifest["git"]["branch"] == "fixture"
    assert len(manifest["git"]["commit"]) == 40
    assert {entry["path"]: entry["status"] for entry in manifest["overlayFiles"]} == {
        "FluidNC/src/base.cpp": "modified",
        "FluidNC/src/new.cpp": "untracked",
        "lib/websocket_compat.h": "deleted",
    }
    assert manifest["overlayFileCount"] == 3
    assert len(manifest["overlayAggregateSha256"]) == 64


def test_manifest_output_is_creation_only(tmp_path):
    tool = load_tool()
    source = tmp_path / "source"
    source.mkdir()
    init_git_fixture(source)
    output = tmp_path / "receipt.json"

    assert tool.main(["--root", str(source), "--output", str(output)]) == 0
    with pytest.raises(FileExistsError):
        tool.main(["--root", str(source), "--output", str(output)])


def test_manifest_binds_gitlink_pointer_and_checked_out_content(tmp_path):
    tool = load_tool()
    dependency = tmp_path / "dependency"
    dependency.mkdir()
    subprocess.run(["git", "init", "-b", "main", str(dependency)], check=True, capture_output=True)
    subprocess.run(["git", "-C", str(dependency), "config", "user.name", "Test"], check=True)
    subprocess.run(["git", "-C", str(dependency), "config", "user.email", "test@example.invalid"], check=True)
    (dependency / "websocket.cpp").write_text("dependency\n", encoding="utf-8")
    subprocess.run(["git", "-C", str(dependency), "add", "."], check=True)
    subprocess.run(["git", "-C", str(dependency), "commit", "-m", "dependency"], check=True, capture_output=True)
    dependency_commit = subprocess.run(
        ["git", "-C", str(dependency), "rev-parse", "HEAD"], check=True, capture_output=True, text=True
    ).stdout.strip()

    source = tmp_path / "source"
    source.mkdir()
    init_git_fixture(source)
    subprocess.run(
        [
            "git",
            "-c",
            "protocol.file.allow=always",
            "-C",
            str(source),
            "submodule",
            "add",
            str(dependency),
            "lib/mengrao_websocket",
        ],
        check=True,
        capture_output=True,
    )
    subprocess.run(["git", "-C", str(source), "commit", "-am", "add gitlink"], check=True, capture_output=True)

    manifest = tool.build_manifest(source)

    assert manifest["gitlinkCount"] == 1
    assert manifest["gitlinks"] == [
        {
            "path": "lib/mengrao_websocket",
            "indexCommit": dependency_commit,
            "contentFileCount": 1,
            "contentAggregateSha256": manifest["gitlinks"][0]["contentAggregateSha256"],
        }
    ]
    assert len(manifest["gitlinkAggregateSha256"]) == 64
