#!/usr/bin/env python3
"""
Cross-platform code coverage for FluidNC host-side tests.

Usage:
    python coverage.py [--html] [--verbose] [--skip-unit] [--skip-machine-buses] [--skip-webui]

Requirements:
    pip install gcovr

This script:
1. Cleans previous coverage data
2. Builds and runs coverage-instrumented suites
3. Generates coverage reports (text, summary JSON, optional HTML)
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

INTEGRATION_SUITES = [
    ("machine_buses", "test_integration_machine_buses"),
    ("webui", "test_integration_webui"),
]


def run(cmd, check=True):
    """Run a command and return success status."""
    print(f"$ {cmd}")
    result = subprocess.run(cmd, shell=True)
    if check and result.returncode != 0:
        return False
    return True


def to_gcovr_path(path):
    """Convert path to gcovr-compatible format (forward slashes)."""
    return str(path).replace("\\", "/")


def clean_gcda(build_dir):
    """Delete stale coverage data files in a build directory."""
    if not build_dir.exists():
        return
    for gcda in build_dir.rglob("*.gcda"):
        gcda.unlink()


def parse_args():
    parser = argparse.ArgumentParser(description="Generate FluidNC coverage reports")
    parser.add_argument("--html", action="store_true", help="Generate coverage.html")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print top uncovered files")
    parser.add_argument("--skip-unit", action="store_true", help="Skip tests_coverage")
    parser.add_argument("--skip-machine-buses", action="store_true", help="Skip test_integration_machine_buses coverage suite")
    parser.add_argument("--skip-webui", action="store_true", help="Skip test_integration_webui coverage suite")
    return parser.parse_args()


def _collect_ini_section(lines, section_name):
    in_section = False
    collected = []
    section_header = f"[{section_name}]"
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if stripped == section_header:
                in_section = True
                continue
            if in_section:
                break
        if in_section:
            collected.append(line.rstrip("\n"))
    return collected


def _collect_build_src_filter_entries(section_lines):
    entries = []
    in_filter = False
    for raw in section_lines:
        stripped = raw.strip()
        if stripped.startswith("build_src_filter"):
            in_filter = True
            if "=" in stripped:
                rhs = stripped.split("=", 1)[1].strip()
                if rhs:
                    entries.append(rhs)
            continue
        if in_filter:
            if not stripped:
                continue
            if raw.startswith(" ") or raw.startswith("\t"):
                entries.append(stripped)
                continue
            break
    return entries


def _resolve_filter_entry(entry, root):
    m = re.match(r"^[+-]<(.+)>$", entry)
    if not m:
        return None
    rel = m.group(1).replace("\\", "/")
    if rel.endswith((".h", ".hpp", ".hh", ".S", ".s")):
        return None
    if "*" in rel:
        return None
    if "/tests/" in rel or rel.startswith("tests/") or "test_main.cpp" in rel:
        return None
    if "Test.cpp" in rel:
        return None
    if not rel.endswith((".c", ".cc", ".cpp")):
        return None

    if rel.startswith("../capture/"):
        return (root / "FluidNC" / "capture" / rel[len("../capture/"):]).resolve()
    if rel.startswith("../tests/"):
        return None
    if rel.startswith("../"):
        # Unknown parent-relative source, skip for active-host metric.
        return None
    return (root / "FluidNC" / "src" / rel).resolve()


def active_host_files_from_platformio(root):
    ini = root / "platformio.ini"
    if not ini.exists():
        return set()

    lines = ini.read_text(encoding="utf-8", errors="ignore").splitlines()
    sections = [
        "tests_common",
        "integration_common",
    ]

    active = set()
    for section in sections:
        section_lines = _collect_ini_section(lines, section)
        if not section_lines:
            continue
        for entry in _collect_build_src_filter_entries(section_lines):
            path = _resolve_filter_entry(entry, root)
            if path is not None and path.exists():
                active.add(str(path).replace("\\", "/"))
    return active


def _map_gcno_to_source(root, build_dir, gcno_path):
    try:
        rel = gcno_path.relative_to(build_dir).as_posix()
    except ValueError:
        return None

    if not rel.endswith(".gcno"):
        return None
    stem = rel[:-5]

    if stem.startswith("src/"):
        source_rel = stem[len("src/"):]
        base = root / "FluidNC" / "src" / source_rel
    elif stem.startswith("capture/"):
        source_rel = stem[len("capture/"):]
        base = root / "FluidNC" / "capture" / source_rel
    else:
        return None

    for ext in (".cpp", ".cc", ".c"):
        candidate = (str(base) + ext)
        candidate_path = Path(candidate)
        if candidate_path.exists():
            return str(candidate_path.resolve()).replace("\\", "/")
    return None


def compiled_source_files_from_build_dirs(root, build_dirs):
    compiled = set()
    for build_dir in build_dirs:
        if not build_dir.exists():
            continue
        for gcno in build_dir.rglob("*.gcno"):
            mapped = _map_gcno_to_source(root, build_dir, gcno)
            if mapped is not None:
                compiled.add(mapped)
    return compiled


def main():
    args = parse_args()

    if shutil.which("gcovr") is None:
        print("ERROR: gcovr not found. Install with: pip install gcovr")
        return 1

    root = Path(__file__).parent.resolve()
    src_dir = root / "FluidNC" / "src"
    capture_dir = root / "FluidNC" / "capture"

    suites = []
    if not args.skip_unit:
        suites.append(
            {
                "name": "unit",
                "build_dir": root / ".pio" / "build" / "tests_coverage",
                "build_cmd": "pio test -e tests_coverage",
                "run_cmd": None,
            }
        )

    selected_integration = []
    for arg_name, suite_name in INTEGRATION_SUITES:
        if getattr(args, f"skip_{arg_name.replace('-', '_')}", False):
            continue
        selected_integration.append(suite_name)

    if selected_integration:
        integration_build = root / ".pio" / "build" / "integration_coverage"
        filter_args = " ".join(f"-f {suite}" for suite in selected_integration)
        suites.append(
            {
                "name": "integration",
                "build_dir": integration_build,
                "build_cmd": f"pio test -e integration_coverage {filter_args}".strip(),
                "run_cmd": None,
            }
        )

    if not suites:
        print("ERROR: No suites selected. Remove --skip-* flags.")
        return 1

    print("\n=== Cleaning previous coverage data ===")
    for suite in suites:
        clean_gcda(suite["build_dir"])

    print("\n=== Building and running coverage suites ===")
    for suite in suites:
        print(f"\n--- {suite['name']} ---")
        if not run(suite["build_cmd"]):
            print(f"ERROR: {suite['name']} build/test failed")
            return 1
        if suite["run_cmd"] and not run(suite["run_cmd"]):
            print(f"ERROR: {suite['name']} executable failed")
            return 1

    print("\n=== Generating coverage reports ===")
    txt_report = root / "coverage.txt"
    branch_txt_report = root / "coverage-branches.txt"
    json_summary = root / "coverage-summary.json"

    gcovr_base = [
        "gcovr",
        f"--root={to_gcovr_path(root)}",
        f"--filter={to_gcovr_path(src_dir)}/",
        f"--filter={to_gcovr_path(capture_dir)}/",
        "--exclude=.*Test\\.cpp$",
        "--exclude=.*/tests/.*",
        "--exclude=.*test_main\\.cpp$",
        "--exclude=.*googletest/.*",
        "--exclude=.*googlemock/.*",
        "--sort",
        "uncovered-number",
        "--sort-reverse",
    ]
    gcovr_base.extend([to_gcovr_path(suite["build_dir"]) for suite in suites])

    if not run(" ".join(gcovr_base + [f"--txt \"{txt_report}\"", "--print-summary"])):
        print("ERROR: Failed to generate text report")
        return 1

    if not run(" ".join(gcovr_base + ["--txt-metric branch", f"--txt \"{branch_txt_report}\""])):
        print("ERROR: Failed to generate branch text report")
        return 1

    if not run(" ".join(gcovr_base + [f"--json-summary \"{json_summary}\"", "--json-summary-pretty"])):
        print("ERROR: Failed to generate JSON summary")
        return 1

    if args.html:
        html_report = root / "coverage.html"
        if not run(
            " ".join(
                gcovr_base
                + [
                    f"--html-details \"{html_report}\"",
                    "--html-title \"FluidNC Coverage (Unit + Machine Buses + WebUI)\"",
                ]
            )
        ):
            print("ERROR: Failed to generate HTML report")
            return 1
        print(f"HTML report: {html_report}")

    print(f"Text report: {txt_report}")
    print(f"Branch report: {branch_txt_report}")
    print(f"JSON summary: {json_summary}")

    if json_summary.exists():
        data = json.loads(json_summary.read_text(encoding="utf-8"))
        files = [
            item
            for item in data.get("files", [])
            if item.get("filename", "").endswith((".c", ".cc", ".cpp"))
        ]

        covered_set = {
            str((root / item.get("filename", "")).resolve()).replace("\\", "/")
            for item in files
        }
        build_dirs = [suite["build_dir"] for suite in suites]
        compiled_set = compiled_source_files_from_build_dirs(root, build_dirs)
        all_cpp = []
        all_cpp.extend((root / "FluidNC" / "src").rglob("*.c"))
        all_cpp.extend((root / "FluidNC" / "src").rglob("*.cpp"))
        all_cpp.extend((root / "FluidNC" / "capture").rglob("*.c"))
        all_cpp.extend((root / "FluidNC" / "capture").rglob("*.cpp"))
        all_set = {str(path.resolve()).replace("\\", "/") for path in all_cpp}
        callable_set = covered_set & all_set
        instrumented_set = (covered_set | compiled_set) & all_set
        missing = sorted(all_set - instrumented_set)
        non_callable_instrumented = instrumented_set - callable_set

        print(
            f"Source file compile coverage inventory: {len(instrumented_set)}/{len(all_set)} "
            f"({(100.0 * len(instrumented_set) / len(all_set)) if all_set else 0.0:.1f}%)"
        )
        print(
            f"Source file callable inventory: {len(callable_set)}/{len(all_set)} "
            f"({(100.0 * len(callable_set) / len(all_set)) if all_set else 0.0:.1f}%)"
        )

        covered_lines_by_file = {}
        for item in files:
            abs_path = str((root / item.get("filename", "")).resolve()).replace("\\", "/")
            covered_lines_by_file[abs_path] = int(item.get("line_covered", 0))

        whole_repo_total_lines = 0
        whole_repo_covered_lines = 0
        for path in sorted(all_set):
            try:
                raw = Path(path).read_text(encoding="utf-8", errors="ignore").splitlines()
            except OSError:
                raw = []
            whole_repo_total_lines += len(raw)
            whole_repo_covered_lines += covered_lines_by_file.get(path, 0)

        whole_repo_line_pct = (
            (100.0 * whole_repo_covered_lines / whole_repo_total_lines)
            if whole_repo_total_lines
            else 0.0
        )
        print(
            "Whole-repo line coverage proxy (covered executable lines / all source lines): "
            f"{whole_repo_covered_lines}/{whole_repo_total_lines} ({whole_repo_line_pct:.1f}%)"
        )

        # In stage 1 we only execute a narrow host matrix, so guardrails should
        # reflect the files compiled by the selected coverage suites rather than
        # the full historical integration host surface.
        active_host_files = instrumented_set
        active_host_instrumented = instrumented_set & active_host_files
        active_host_callable = callable_set & active_host_files
        active_host_called = {
            path
            for path in active_host_callable
            if covered_lines_by_file.get(path, 0) > 0
        }

        active_host_instrumented_pct = (
            (100.0 * len(active_host_instrumented) / len(active_host_files))
            if active_host_files
            else 0.0
        )
        active_host_callable_pct = (
            (100.0 * len(active_host_callable) / len(active_host_files))
            if active_host_files
            else 0.0
        )
        active_host_called_pct = (
            (100.0 * len(active_host_called) / len(active_host_callable))
            if active_host_callable
            else 0.0
        )
        active_host_called_of_total_pct = (
            (100.0 * len(active_host_called) / len(active_host_files))
            if active_host_files
            else 0.0
        )
        print(
            "Active-host file coverage (from selected coverage suites): "
            f"instrumented {len(active_host_instrumented)}/{len(active_host_files)} "
            f"({active_host_instrumented_pct:.1f}%), "
            f"callable {len(active_host_callable)}/{len(active_host_files)} "
            f"({active_host_callable_pct:.1f}%), "
            f"called {len(active_host_called)}/{len(active_host_callable)} "
            f"({active_host_called_pct:.1f}%)"
        )

        fluidnc_root = str((root / "FluidNC").resolve()).replace("\\", "/") + "/"

        def rel_to_fluidnc(abs_path):
            return abs_path.replace(fluidnc_root, "")

        buckets = {}
        for path in sorted(all_set):
            rel = rel_to_fluidnc(path)
            parts = rel.split("/")
            bucket = "/".join(parts[:2]) if len(parts) >= 2 else rel
            buckets.setdefault(bucket, {"total": 0, "instrumented": 0, "missing": 0, "missing_files": []})
            buckets[bucket]["total"] += 1

        for path in sorted(instrumented_set):
            rel = rel_to_fluidnc(path)
            parts = rel.split("/")
            bucket = "/".join(parts[:2]) if len(parts) >= 2 else rel
            if bucket in buckets:
                buckets[bucket]["instrumented"] += 1

        for path in missing:
            rel = rel_to_fluidnc(path)
            parts = rel.split("/")
            bucket = "/".join(parts[:2]) if len(parts) >= 2 else rel
            if bucket in buckets:
                buckets[bucket]["missing"] += 1
                if len(buckets[bucket]["missing_files"]) < 5:
                    buckets[bucket]["missing_files"].append(rel)

        gaps_json = root / "coverage-gaps.json"
        bucket_rows = []
        for bucket, stats in sorted(
            buckets.items(),
            key=lambda kv: (kv[1]["missing"], kv[1]["total"]),
            reverse=True,
        ):
            total = stats["total"]
            instrumented = stats["instrumented"]
            pct = (100.0 * instrumented / total) if total else 0.0
            bucket_rows.append(
                {
                    "bucket": bucket,
                    "total": total,
                    "instrumented": instrumented,
                    "missing": stats["missing"],
                    "instrumented_percent": round(pct, 1),
                    "missing_files_sample": stats["missing_files"],
                }
            )

        low_coverage = sorted(files, key=lambda item: item.get("line_percent", 100.0))[:25]
        low_coverage_rows = [
            {
                "filename": item.get("filename", ""),
                "line_percent": item.get("line_percent", 0.0),
                "line_covered": item.get("line_covered", 0),
                "line_total": item.get("line_total", 0),
            }
            for item in low_coverage
        ]

        gaps_payload = {
            "source_files_total": len(all_set),
            "source_files_instrumented": len(instrumented_set),
            "source_files_missing": len(missing),
            "source_files_instrumented_percent": round((100.0 * len(instrumented_set) / len(all_set)) if all_set else 0.0, 1),
            "source_files_callable": len(callable_set),
            "source_files_callable_percent": round((100.0 * len(callable_set) / len(all_set)) if all_set else 0.0, 1),
            "source_files_non_callable_instrumented": len(non_callable_instrumented),
            "whole_repo_line_proxy": {
                "covered_lines": whole_repo_covered_lines,
                "total_lines": whole_repo_total_lines,
                "line_percent": round(whole_repo_line_pct, 1),
            },
            "active_host_files": {
                "total": len(active_host_files),
                "instrumented": len(active_host_instrumented),
                "callable_total": len(active_host_callable),
                "called": len(active_host_called),
                "instrumented_percent": round(active_host_instrumented_pct, 1),
                "callable_percent": round(active_host_callable_pct, 1),
                "called_percent": round(active_host_called_pct, 1),
                "called_percent_of_total": round(active_host_called_of_total_pct, 1),
                "missing_sample": [rel_to_fluidnc(path) for path in sorted(active_host_files - active_host_instrumented)[:50]],
                "non_callable_sample": [rel_to_fluidnc(path) for path in sorted(active_host_instrumented - active_host_callable)[:50]],
                "uncalled_sample": [rel_to_fluidnc(path) for path in sorted(active_host_callable - active_host_called)[:50]],
            },
            "bucket_summary": bucket_rows,
            "missing_files_sample": [rel_to_fluidnc(path) for path in missing[:100]],
            "lowest_coverage_instrumented_files": low_coverage_rows,
        }
        gaps_json.write_text(json.dumps(gaps_payload, indent=2), encoding="utf-8")
        print(f"Gap summary: {gaps_json}")

        if args.verbose:
            ranked = sorted(files, key=lambda item: item.get("line_percent", 100.0))
            print("\nTop uncovered files by line coverage:")
            for item in ranked[:10]:
                name = item.get("filename", "")
                line_pct = item.get("line_percent", 0.0)
                line_total = item.get("line_total", 0)
                line_cov = item.get("line_covered", 0)
                print(f"  {line_pct:6.2f}% ({line_cov}/{line_total})  {name}")

            print("\nTop uncovered source buckets:")
            for row in bucket_rows[:10]:
                print(
                    f"  {row['bucket']}: missing {row['missing']}/{row['total']} "
                    f"(covered {row['instrumented_percent']:.1f}%)"
                )
                if row["missing_files_sample"]:
                    sample = ", ".join(row["missing_files_sample"][:3])
                    print(f"    sample: {sample}")

            if missing:
                print("\nSample uncovered source files:")
                for rel in [rel_to_fluidnc(path) for path in missing[:20]]:
                    print(f"  {rel}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
