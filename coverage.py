#!/usr/bin/env python3
"""
Cross-platform code coverage for FluidNC unit tests.

Usage:
    python coverage.py [--html] [--verbose]

Requirements:
    pip install gcovr

This script:
1. Cleans previous coverage data
2. Builds and runs tests with coverage instrumentation
3. Generates a coverage report (text or HTML)
"""

import subprocess
import sys
import shutil
import os
from pathlib import Path

def run(cmd, check=True):
    """Run a command and return success status."""
    print(f"$ {cmd}")
    # Use shell=True for simple string commands (pio, python are trusted)
    # Alternative: split cmd properly for list-based subprocess.run
    result = subprocess.run(cmd, shell=True)
    if check and result.returncode != 0:
        return False
    return True

def to_gcovr_path(path):
    """Convert path to gcovr-compatible format (forward slashes)."""
    return str(path).replace("\\", "/")

def main():
    html_output = "--html" in sys.argv
    verbose = "--verbose" in sys.argv or "-v" in sys.argv
    
    # Check for gcovr
    if shutil.which("gcovr") is None:
        print("ERROR: gcovr not found. Install with: pip install gcovr")
        return 1
    
    root = Path(__file__).parent.resolve()
    build_dir = root / ".pio" / "build" / "tests_coverage"
    src_dir = root / "FluidNC" / "src"
    
    # Step 1: Clean previous coverage data
    print("\n=== Cleaning previous coverage data ===")
    for gcda in build_dir.rglob("*.gcda"):
        gcda.unlink()
    
    # Step 2: Build and run tests
    print("\n=== Building and running tests with coverage ===")
    if not run("pio test -e tests_coverage"):
        print("ERROR: Tests failed")
        return 1
    
    # Step 3: Generate coverage report
    print("\n=== Generating coverage report ===")
    
    # Filter to only our source files, exclude test files and googletest
    # Use forward slashes for gcovr compatibility
    gcovr_args = [
        "gcovr",
        f"--root={to_gcovr_path(root)}",
        f"--filter={to_gcovr_path(src_dir)}/",
        "--exclude=.*Test\\.cpp$",
        "--exclude=.*/tests/.*",
        "--exclude=.*test_main\\.cpp$",
    ]
    
    if html_output:
        output_file = root / "coverage.html"
        gcovr_args.extend([
            "--html-details", str(output_file),
            "--html-title", "FluidNC Test Coverage",
        ])
        print(f"Generating HTML report: {output_file}")
    else:
        # Default: show summary with branch coverage
        gcovr_args.extend(["--print-summary"])
        if verbose:
            # Verbose: add detailed line-by-line output
            gcovr_args.extend(["--branches", "--txt"])
    
    gcovr_args.append(to_gcovr_path(build_dir))
    
    result = subprocess.run(gcovr_args)
    
    if html_output and result.returncode == 0:
        print(f"\nOpen {output_file} in a browser to view the report")
    
    # gcovr returns 0 on success
    return 0 if result.returncode == 0 else result.returncode

if __name__ == "__main__":
    sys.exit(main())
