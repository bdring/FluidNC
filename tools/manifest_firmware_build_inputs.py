#!/usr/bin/env python3
"""Create a canonical manifest of FluidNC WiFi firmware project inputs.

The manifest binds both the complete selected project input set and the dirty
Git overlay (including untracked firmware and dependency-patcher files).  It
does not include generated ``.pio`` objects or documentation/test tooling.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


EXACT_INPUTS = {
    "platformio.ini",
    "platformio_override.ini",
    "git-version.py",
    "min_littlefs.csv",
}
INPUT_ROOTS = (
    "FluidNC/src",
    "FluidNC/esp32",
    "FluidNC/include",
    "FluidNC/ld",
    "FluidNC/stdfs",
    "lib",
    "libraries",
)


def is_selected(relative: str) -> bool:
    relative = relative.replace("\\", "/")
    if relative in EXACT_INPUTS:
        return True
    if any(relative == root or relative.startswith(root + "/") for root in INPUT_ROOTS):
        return True
    return relative.startswith("tools/patch_") and relative.endswith(".py")


def discover_inputs(root: Path) -> list[Path]:
    selected = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in {".git", ".pio", "__pycache__"} for part in path.relative_to(root).parts):
            continue
        relative = path.relative_to(root).as_posix()
        if is_selected(relative):
            selected.append(path)
    return sorted(selected, key=lambda path: path.relative_to(root).as_posix())


def file_entry(root: Path, path: Path) -> dict[str, Any]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    return {
        "path": path.relative_to(root).as_posix(),
        "bytes": size,
        "sha256": digest.hexdigest().upper(),
    }


def aggregate_entries(entries: Iterable[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for entry in sorted(entries, key=lambda item: item["path"]):
        digest.update(entry["path"].encode("utf-8"))
        digest.update(b"\0")
        if entry.get("deleted"):
            digest.update(b"DELETED")
        elif entry.get("gitlink"):
            digest.update(b"GITLINK\0")
            digest.update(bytes.fromhex(entry["gitlink"]["indexCommit"]))
            digest.update(b"\0")
            digest.update(bytes.fromhex(entry["gitlink"]["contentAggregateSha256"]))
        else:
            digest.update(str(entry["bytes"]).encode("ascii"))
            digest.update(b"\0")
            digest.update(bytes.fromhex(entry["sha256"]))
        digest.update(b"\n")
    return digest.hexdigest().upper()


def _git(root: Path, *arguments: str) -> bytes:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return completed.stdout


def _git_text(root: Path, *arguments: str) -> str:
    return _git(root, *arguments).decode("utf-8").strip()


def _git_paths(root: Path, *arguments: str) -> set[str]:
    raw = _git(root, *arguments)
    return {
        item.decode("utf-8").replace("\\", "/")
        for item in raw.split(b"\0")
        if item and is_selected(item.decode("utf-8"))
    }


def gitlinks(root: Path, entries: list[dict[str, Any]]) -> list[dict[str, Any]]:
    links = []
    raw = _git(root, "ls-files", "--stage", "-z")
    for record in raw.split(b"\0"):
        if not record or b"\t" not in record:
            continue
        metadata, encoded_path = record.split(b"\t", 1)
        mode, object_id, _stage = metadata.decode("ascii").split(" ")
        relative = encoded_path.decode("utf-8").replace("\\", "/")
        if mode != "160000" or not is_selected(relative):
            continue
        content_entries = [entry for entry in entries if entry["path"].startswith(relative + "/")]
        links.append(
            {
                "path": relative,
                "indexCommit": object_id,
                "contentFileCount": len(content_entries),
                "contentAggregateSha256": aggregate_entries(content_entries),
            }
        )
    return sorted(links, key=lambda link: link["path"])


def aggregate_gitlinks(links: Iterable[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for link in sorted(links, key=lambda item: item["path"]):
        digest.update(link["path"].encode("utf-8"))
        digest.update(b"\0")
        digest.update(bytes.fromhex(link["indexCommit"]))
        digest.update(b"\0")
        digest.update(bytes.fromhex(link["contentAggregateSha256"]))
        digest.update(b"\n")
    return digest.hexdigest().upper()


def build_manifest(root: Path) -> dict[str, Any]:
    root = root.resolve()
    entries = [file_entry(root, path) for path in discover_inputs(root)]
    by_path = {entry["path"]: entry for entry in entries}
    links = gitlinks(root, entries)
    links_by_path = {link["path"]: link for link in links}

    changed = _git_paths(root, "diff", "--name-only", "-z", "HEAD", "--")
    untracked = _git_paths(root, "ls-files", "--others", "--exclude-standard", "-z")
    deleted = _git_paths(root, "diff", "--name-only", "--diff-filter=D", "-z", "HEAD", "--")
    overlay_entries = []
    for relative in sorted((changed | untracked) - deleted):
        if relative in by_path:
            entry = dict(by_path[relative])
        elif relative in links_by_path:
            entry = {"path": relative, "gitlink": links_by_path[relative]}
        else:
            raise RuntimeError(f"selected overlay input is absent from file and gitlink manifests: {relative}")
        entry["status"] = "untracked" if relative in untracked else "modified"
        overlay_entries.append(entry)
    for relative in sorted(deleted):
        overlay_entries.append({"path": relative, "deleted": True, "status": "deleted"})

    return {
        "schema": 1,
        "generatedUtc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "sourceRoot": str(root),
        "git": {
            "remoteOrigin": _git_text(root, "remote", "get-url", "origin"),
            "branch": _git_text(root, "branch", "--show-current"),
            "commit": _git_text(root, "rev-parse", "HEAD"),
        },
        "selection": {
            "exact": sorted(EXACT_INPUTS),
            "roots": list(INPUT_ROOTS),
            "patcherGlob": "tools/patch_*.py",
            "excluded": [".pio generated output", "documentation", "tests", "non-patcher host tools"],
        },
        "inputFileCount": len(entries),
        "inputAggregateSha256": aggregate_entries(entries),
        "gitlinkCount": len(links),
        "gitlinkAggregateSha256": aggregate_gitlinks(links),
        "gitlinks": links,
        "overlayFileCount": len(overlay_entries),
        "overlayAggregateSha256": aggregate_entries(overlay_entries),
        "overlayFiles": overlay_entries,
        "files": entries,
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    manifest = build_manifest(args.root)
    payload = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
        print(args.output.resolve())
    else:
        print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
