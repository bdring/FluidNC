#!/usr/bin/env python3
"""
Coverage guardrails for critical FluidNC modules.

Fails with non-zero exit code when any critical module drops below configured
minimum line coverage, or when active-host called-file coverage drops below
threshold.
"""

import argparse
import json
import sys
from pathlib import Path


CRITICAL_MIN_LINE_COVERAGE = {
    "FluidNC/src/Machine/I2CBus.cpp": 70.0,
    "FluidNC/src/Machine/I2SOBus.cpp": 60.0,
    "FluidNC/src/Machine/SPIBus.cpp": 60.0,
    "FluidNC/src/Machine/MachineConfig.cpp": 25.0,
    "FluidNC/src/WebUI/Mdns.cpp": 74.0,
    "FluidNC/src/WebUI/NotificationsService.cpp": 40.0,
}

# One-line rationale per threshold so guardrails are auditable and intentional.
CRITICAL_COVERAGE_RATIONALE = {
    "FluidNC/src/Machine/I2CBus.cpp": "Stage 1 bus rollout must keep host coverage on I2C init and transfer behavior.",
    "FluidNC/src/Machine/I2SOBus.cpp": "Stage 1 bus rollout exercises I2SO validation and init wiring from host integration tests.",
    "FluidNC/src/Machine/SPIBus.cpp": "SPI bus config and fallback pin behavior are part of the stage 1 host safety net.",
    "FluidNC/src/Machine/MachineConfig.cpp": "MachineConfig is only partially exercised in stage 1, but the guard should still detect accidental loss of that coverage.",
    "FluidNC/src/WebUI/Mdns.cpp": "Stage 1 covers mDNS startup and service registration on the shared host surface, but not every error branch.",
    "FluidNC/src/WebUI/NotificationsService.cpp": "Stage 1 verifies backend dispatch behavior for notifications, not the full settings/bootstrap path.",
}

MIN_ACTIVE_HOST_CALLED_PERCENT = 60.0


def parse_args():
    parser = argparse.ArgumentParser(description="Coverage guardrails for critical modules")
    parser.add_argument("--summary", default="coverage-summary.json", help="Path to gcovr JSON summary")
    parser.add_argument("--gaps", default="coverage-gaps.json", help="Path to coverage gaps JSON")
    return parser.parse_args()


def _load_json(path: Path, label: str):
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"Unable to read {label} file {path}: {exc}") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in {label} file {path}: {exc}") from exc


def _validate_summary(summary):
    if not isinstance(summary, dict):
        raise ValueError("coverage summary must be a JSON object")
    files = summary.get("files")
    if not isinstance(files, list):
        raise ValueError("coverage summary must contain a 'files' array")
    for entry in files:
        if not isinstance(entry, dict):
            raise ValueError("coverage summary 'files' entries must be JSON objects")
        if "filename" not in entry:
            raise ValueError("coverage summary file entry is missing 'filename'")
        if "line_percent" not in entry:
            raise ValueError("coverage summary file entry is missing 'line_percent'")
        try:
            float(entry["line_percent"])
        except (TypeError, ValueError) as exc:
            raise ValueError(
                f"coverage summary line_percent for {entry.get('filename', '<unknown>')} is not numeric"
            ) from exc


def _validate_gaps(gaps):
    if not isinstance(gaps, dict):
        raise ValueError("coverage gaps must be a JSON object")
    active = gaps.get("active_host_files")
    if not isinstance(active, dict):
        raise ValueError("coverage gaps must contain an 'active_host_files' object")
    metric_name = "called_percent_of_total" if "called_percent_of_total" in active else "called_percent"
    if metric_name not in active:
        raise ValueError("coverage gaps active_host_files is missing called coverage percentage")
    try:
        float(active[metric_name])
    except (TypeError, ValueError) as exc:
        raise ValueError(f"coverage gaps active_host_files.{metric_name} is not numeric") from exc


def main():
    args = parse_args()
    return main_with_paths(Path(args.summary), Path(args.gaps))


def main_with_paths(summary_path: Path, gaps_path: Path):
    if not summary_path.exists():
        print(f"ERROR: Missing coverage summary: {summary_path}")
        return 2
    if not gaps_path.exists():
        print(f"ERROR: Missing coverage gaps: {gaps_path}")
        return 2

    try:
        summary = _load_json(summary_path, "coverage summary")
        gaps = _load_json(gaps_path, "coverage gaps")
        _validate_summary(summary)
        _validate_gaps(gaps)
    except ValueError as exc:
        print(f"ERROR: {exc}")
        return 2

    by_name = {entry.get("filename", ""): entry for entry in summary.get("files", [])}

    failures = []
    for filename, min_pct in CRITICAL_MIN_LINE_COVERAGE.items():
        entry = by_name.get(filename)
        if entry is None:
            failures.append(f"{filename}: missing from coverage summary")
            continue
        line_pct = float(entry.get("line_percent", 0.0))
        if line_pct < min_pct:
            failures.append(f"{filename}: {line_pct:.1f}% < {min_pct:.1f}%")

    active = gaps.get("active_host_files", {})
    metric_name = "called_percent_of_total" if "called_percent_of_total" in active else "called_percent"
    called_pct = float(active.get(metric_name, 0.0))
    if called_pct < MIN_ACTIVE_HOST_CALLED_PERCENT:
        failures.append(
            f"active_host_files.{metric_name}: "
            f"{called_pct:.1f}% < {MIN_ACTIVE_HOST_CALLED_PERCENT:.1f}%"
        )

    if failures:
        print("Coverage guard failed:")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("Coverage guard passed.")
    for filename, min_pct in CRITICAL_MIN_LINE_COVERAGE.items():
        print(f" - {filename} >= {min_pct:.1f}% ({CRITICAL_COVERAGE_RATIONALE[filename]})")
    print(f" - active_host_files.{metric_name} >= {MIN_ACTIVE_HOST_CALLED_PERCENT:.1f}%")
    return 0


if __name__ == "__main__":
    sys.exit(main())
