#!/usr/bin/env python3
"""
validate_fluidnc_config.py — validate a FluidNC config.yaml against
fluidnc-config-schema.json.

Usage:
    python3 validate_fluidnc_config.py config.yaml
    python3 validate_fluidnc_config.py config.yaml --schema /path/to/fluidnc-config-schema.json
    python3 validate_fluidnc_config.py config.yaml --json             # machine-readable output
    python3 validate_fluidnc_config.py config.yaml --permissive       # see below
    python3 validate_fluidnc_config.py config.yaml --no-auto-install  # fail instead of installing deps

Strict vs permissive mode:
    FluidNC's real parser matches most identifiers case-insensitively (e.g.
    'pwm'/'PWM', 'corexy'/'CoreXY' are equivalent to it). The schema enforces
    one canonical casing. Default (STRICT) mode reports casing mismatches as
    errors. --permissive normalizes known case-insensitive identifiers to
    canonical casing before validating, and reports what it normalized as
    WARNINGS instead — useful when validating existing/human-written configs
    where casing style shouldn't block a merge. See fluidnc_validate_core.py
    for exactly which identifiers this covers.

Exit codes:
    0  valid (no errors; warnings, if any in --permissive mode, don't affect this)
    1  schema violations found
    2  file not found / YAML parse error / schema load error / dependency install failed

Dependencies (auto-installed on first run unless --no-auto-install is given):
    pyyaml, jsonschema

This script only checks structural/type/range/enum correctness against the
schema. It does NOT check the YAML-syntax-level rules from the companion
markdown spec (§0: indentation consistency, no-tabs, etc.) — a file with
inconsistent indentation may fail to parse as YAML at all before this script
even runs, which is itself a signal worth surfacing (see the YAMLError
handling below). It also does not check board-specific pin legality
(which GPIO numbers exist on a given board) — that is intentionally out of
scope, per the schema's own top-level "description".
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

REQUIRED_PACKAGES = ["pyyaml", "jsonschema"]

# Auto-install can be disabled with --no-auto-install / --offline. argparse
# hasn't run yet at import time, so do a minimal pre-scan of argv here.
_AUTO_INSTALL = not any(flag in sys.argv for flag in ("--no-auto-install", "--offline"))


def _pip_install(packages: list[str]) -> bool:
    """Try a plain pip install; on an externally-managed-environment error,
    retry with --break-system-packages. Returns True on success. Any other
    failure (network error, bad package name, permissions, etc.) is reported
    as-is rather than masked by an unconditional retry."""
    base_cmd = [sys.executable, "-m", "pip", "install", "--quiet"] + packages
    result = subprocess.run(base_cmd, capture_output=True, text=True)
    if result.returncode == 0:
        return True
    # Only retry with --break-system-packages for the specific PEP 668
    # "externally-managed-environment" case (common on modern
    # Debian/Ubuntu/Homebrew Pythons) -- not for any other failure reason.
    if "externally-managed-environment" in (result.stderr or ""):
        retry_cmd = base_cmd + ["--break-system-packages"]
        result2 = subprocess.run(retry_cmd, capture_output=True, text=True)
        if result2.returncode == 0:
            return True
        print(result2.stderr or result.stderr, file=sys.stderr)
        return False
    print(result.stderr, file=sys.stderr)
    return False


def _ensure_import(module_name: str, pip_name: str):
    """Import module_name, auto-installing all REQUIRED_PACKAGES on failure (unless disabled)."""
    try:
        return __import__(module_name)
    except ImportError:
        if not _AUTO_INSTALL:
            print(f"Missing dependency: {pip_name}. Install with:\n"
                  f"  pip install {pip_name} --break-system-packages\n"
                  f"(or re-run without --no-auto-install to install it automatically)",
                  file=sys.stderr)
            sys.exit(2)
        print(f"Installing missing dependencies: {', '.join(REQUIRED_PACKAGES)} ...", file=sys.stderr)
        if not _pip_install(REQUIRED_PACKAGES):
            print(f"error: automatic installation failed. Install manually with:\n"
                  f"  pip install {' '.join(REQUIRED_PACKAGES)} --break-system-packages", file=sys.stderr)
            sys.exit(2)
        try:
            return __import__(module_name)
        except ImportError:
            print(f"error: {pip_name} was installed but still cannot be imported "
                  f"(possibly a different Python environment than {sys.executable}).", file=sys.stderr)
            sys.exit(2)


yaml = _ensure_import("yaml", "pyyaml")
_ensure_import("jsonschema", "jsonschema")

# fluidnc_validate_core.py must ship alongside this script (same directory).
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from fluidnc_validate_core import validate_document
except ImportError:
    print("error: fluidnc_validate_core.py not found next to this script. "
          "It must be shipped alongside validate_fluidnc_config.py.", file=sys.stderr)
    sys.exit(2)

DEFAULT_SCHEMA_NAME = "fluidnc-config-schema.json"


def find_default_schema(config_path: Path) -> Path:
    """Look for the schema next to the script, then next to the config file."""
    script_dir_schema = Path(__file__).resolve().parent / DEFAULT_SCHEMA_NAME
    if script_dir_schema.exists():
        return script_dir_schema
    config_dir_schema = config_path.resolve().parent / DEFAULT_SCHEMA_NAME
    if config_dir_schema.exists():
        return config_dir_schema
    return script_dir_schema  # will fail with a clear error below


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate a FluidNC config.yaml against fluidnc-config-schema.json")
    ap.add_argument("config", type=Path, help="Path to the config.yaml file to validate")
    ap.add_argument("--schema", type=Path, default=None,
                     help=f"Path to {DEFAULT_SCHEMA_NAME} (default: look next to this script, then next to the config file)")
    ap.add_argument("--json", action="store_true", help="Emit machine-readable JSON output instead of text")
    ap.add_argument("--permissive", action="store_true",
                     help="Normalize known case-insensitive identifiers before validating; report them as warnings instead of errors")
    ap.add_argument("--no-auto-install", "--offline", action="store_true", dest="no_auto_install",
                     help="Do not attempt to pip install missing dependencies; fail with instructions instead")
    args = ap.parse_args()

    if not args.config.exists():
        print(f"error: config file not found: {args.config}", file=sys.stderr)
        return 2

    schema_path = args.schema or find_default_schema(args.config)
    if not schema_path.exists():
        print(f"error: schema file not found: {schema_path}\n"
              f"  Pass --schema /path/to/{DEFAULT_SCHEMA_NAME} explicitly.", file=sys.stderr)
        return 2

    try:
        with open(schema_path) as f:
            schema = json.load(f)
    except json.JSONDecodeError as e:
        print(f"error: schema file is not valid JSON: {schema_path}\n  {e}", file=sys.stderr)
        return 2

    try:
        with open(args.config) as f:
            doc = yaml.safe_load(f)
    except yaml.YAMLError as e:
        # A YAML parse failure often IS a real config bug (bad indentation, etc.)
        # even though it's outside what the JSON Schema itself can check.
        msg = f"YAML parse error in {args.config}: {e}"
        if args.json:
            print(json.dumps({"valid": False, "yaml_parse_error": str(e)}, indent=2))
        else:
            print(msg, file=sys.stderr)
        return 2

    if doc is None:
        doc = {}

    result = validate_document(doc, schema, permissive=args.permissive)
    errors = result["errors"]
    warnings = result["warnings"]

    if args.json:
        payload = {
            "valid": result["valid"],
            "config": str(args.config),
            "schema": str(schema_path),
            "permissive": args.permissive,
            "errors": errors,
            "warnings": warnings,
        }
        print(json.dumps(payload, indent=2))
        return 0 if not errors else 1

    mode_note = " (permissive mode)" if args.permissive else ""

    if warnings:
        print(f"{len(warnings)} normalization warning(s){mode_note}:\n")
        for w in warnings:
            path = " -> ".join(str(p) for p in w["path"]) or "(root)"
            print(f"  [{path}]")
            print(f"    {w['message']}\n")

    if not errors:
        print(f"OK: {args.config} is valid against {schema_path.name}{mode_note}")
        return 0

    print(f"FAIL: {args.config} has {len(errors)} schema violation(s){mode_note}:\n")
    for e in errors:
        path = " -> ".join(str(p) for p in e["path"]) or "(root)"
        print(f"  [{path}]")
        print(f"    {e['message']}\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
