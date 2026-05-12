#!/usr/bin/env python3
"""
Generate a .addrinfo file from an ESP32 ELF firmware binary.

The .addrinfo file is a compact JSON representation of all text (code)
symbols and their source file/line mappings, designed to be loaded by
a browser-based stack trace decoder without needing the full ELF file
or a server-side addr2line tool.

Usage:
    python generate_addrinfo.py <elf_file> [options]

The script requires the Xtensa (or RISC-V) toolchain's `nm` and
`addr2line` to be available, either in PATH or via --toolchain-prefix.

It will auto-detect the toolchain from the ELF file's architecture
or from a PlatformIO installation.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


def find_tool(tool_name, toolchain_prefix=None, mcu=None):
    """Find a toolchain tool (nm, addr2line) by searching common locations."""
    candidates = []

    if toolchain_prefix:
        candidates.append(f"{toolchain_prefix}{tool_name}")

    # Try MCU-specific prefixes
    prefixes = []
    if mcu:
        if "s3" in mcu:
            prefixes = ["xtensa-esp32s3-elf-", "xtensa-esp-elf-"]
        elif "c3" in mcu or "c6" in mcu or "h2" in mcu:
            prefixes = ["riscv32-esp-elf-"]
        else:
            prefixes = ["xtensa-esp32-elf-", "xtensa-esp-elf-"]
    else:
        prefixes = [
            "xtensa-esp32-elf-",
            "xtensa-esp32s3-elf-",
            "xtensa-esp-elf-",
            "riscv32-esp-elf-",
        ]

    for prefix in prefixes:
        candidates.append(f"{prefix}{tool_name}")

    # Search in PATH
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return found

    # Search in PlatformIO toolchain directories
    pio_dir = Path.home() / ".platformio" / "packages"
    if pio_dir.exists():
        for pkg_dir in sorted(pio_dir.iterdir(), reverse=True):
            if "toolchain" in pkg_dir.name:
                for candidate in candidates:
                    tool_path = pkg_dir / "bin" / candidate
                    if tool_path.exists():
                        return str(tool_path)

    # Fallback: generic
    found = shutil.which(tool_name)
    if found:
        return found

    return None


def detect_mcu(elf_path, nm_tool=None):
    """Try to detect MCU type from the ELF file."""
    try:
        result = subprocess.run(
            ["file", str(elf_path)], capture_output=True, text=True
        )
        output = result.stdout.lower()
        if "xtensa" in output:
            # Could be esp32 or esp32s3, default to esp32
            return "esp32"
        elif "risc-v" in output or "riscv" in output:
            return "esp32c3"
    except FileNotFoundError:
        pass
    return "esp32"


def get_text_symbols(nm_tool, elf_path):
    """
    Extract all text (code) symbols from the ELF using nm.

    Returns a sorted list of (address, size_or_None, name) tuples.
    """
    cmd = [nm_tool, "-n", "-S", "-C", str(elf_path)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error running nm: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    symbols = []
    # nm -n -S output formats:
    #   addr size type name       (when size is known)
    #   addr type name            (when size is unknown)
    pattern = re.compile(
        r"^([0-9a-fA-F]{8,16})\s+(?:([0-9a-fA-F]+)\s+)?([tTwW])\s+(.+)$"
    )

    for line in result.stdout.splitlines():
        m = pattern.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        size = int(m.group(2), 16) if m.group(2) else None
        # sym_type = m.group(3)
        name = m.group(4).strip()
        if addr == 0:
            continue
        symbols.append((addr, size, name))

    # Already sorted by -n, but ensure it
    symbols.sort(key=lambda s: s[0])
    return symbols


def get_source_info_batch(addr2line_tool, elf_path, addresses, batch_size=500):
    """
    Query addr2line for source file and line info for a batch of addresses.

    Returns a dict mapping address -> (filename, line_number).
    """
    result_map = {}

    for i in range(0, len(addresses), batch_size):
        batch = addresses[i : i + batch_size]
        hex_addrs = [f"0x{addr:x}" for addr in batch]

        cmd = [addr2line_tool, "-e", str(elf_path), "-C", "-f"] + hex_addrs
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"Warning: addr2line error: {proc.stderr}", file=sys.stderr)
            continue

        lines = proc.stdout.splitlines()
        # addr2line outputs pairs: function_name\nfile:line
        idx = 0
        for addr in batch:
            if idx + 1 < len(lines):
                # function_name = lines[idx]  # We already have this from nm
                file_line = lines[idx + 1]
                idx += 2
            else:
                file_line = "??:0"
                idx += 2

            # Parse "file:line" or "file:line (discriminator N)"
            file_line = re.sub(r"\s*\(discriminator.*\)", "", file_line)
            if ":" in file_line:
                parts = file_line.rsplit(":", 1)
                filename = parts[0]
                try:
                    line_num = int(parts[1])
                except ValueError:
                    line_num = 0
            else:
                filename = file_line
                line_num = 0

            result_map[addr] = (filename, line_num)

    return result_map


def shorten_path(filepath):
    """Shorten common build paths to make the output more readable."""
    # Remove common CI build prefixes
    replacements = [
        (
            "/home/runner/work/esp32-arduino-lib-builder/esp32-arduino-lib-builder/esp-idf/",
            "idf/",
        ),
        (
            "/home/runner/work/esp32-arduino-lib-builder/esp32-arduino-lib-builder/",
            "arduino/",
        ),
        ("/Users/ficeto/Desktop/ESP32/ESP32S2/esp-idf-public/", "idf/"),
    ]
    for prefix, replacement in replacements:
        if filepath.startswith(prefix):
            return replacement + filepath[len(prefix) :]

    # Try to find FluidNC src path and shorten it
    fnc_match = re.search(r"(/FluidNC/|/src/|\.pio/)", filepath)
    if fnc_match:
        idx = filepath.find("/src/")
        if idx >= 0:
            return "src/" + filepath[idx + 5 :]

    return filepath


def generate_addrinfo(elf_path, output_path=None, mcu=None, build=None, tag=None,
                      toolchain_prefix=None, shorten_paths=True, verbose=False):
    """Generate an .addrinfo file from an ELF binary."""
    elf_path = Path(elf_path)
    if not elf_path.exists():
        print(f"Error: ELF file not found: {elf_path}", file=sys.stderr)
        sys.exit(1)

    if mcu is None:
        mcu = detect_mcu(elf_path)

    nm_tool = find_tool("nm", toolchain_prefix, mcu)
    addr2line_tool = find_tool("addr2line", toolchain_prefix, mcu)

    if not nm_tool:
        print("Error: Could not find 'nm' tool. Install ESP toolchain or use --toolchain-prefix.", file=sys.stderr)
        sys.exit(1)
    if not addr2line_tool:
        print("Error: Could not find 'addr2line' tool. Install ESP toolchain or use --toolchain-prefix.", file=sys.stderr)
        sys.exit(1)

    if verbose:
        print(f"Using nm:        {nm_tool}")
        print(f"Using addr2line: {addr2line_tool}")
        print(f"MCU:             {mcu}")

    # Step 1: Get all text symbols
    if verbose:
        print("Extracting symbols with nm...")
    symbols = get_text_symbols(nm_tool, elf_path)
    if verbose:
        print(f"  Found {len(symbols)} text symbols")

    # Step 2: Get source info for all symbol addresses
    if verbose:
        print("Querying addr2line for source info...")
    addresses = [addr for addr, _, _ in symbols]
    source_info = get_source_info_batch(addr2line_tool, elf_path, addresses)
    if verbose:
        resolved = sum(1 for _, (f, _) in source_info.items() if f != "??")
        print(f"  Resolved {resolved}/{len(symbols)} symbols to source")

    # Step 3: Build the addrinfo structure
    # Each entry: [address, function_name, source_file, line_number]
    # Sorted by address for binary search on the client side
    entries = []
    for addr, size, name in symbols:
        src_file, src_line = source_info.get(addr, ("??", 0))
        if shorten_paths and src_file != "??":
            src_file = shorten_path(src_file)
        entry = [addr, name, src_file, src_line]
        if size is not None:
            entry.append(size)
        entries.append(entry)

    addrinfo = {
        "version": 1,
        "mcu": mcu,
        "symbols": entries,
    }
    if build:
        addrinfo["build"] = build
    if tag:
        addrinfo["tag"] = tag

    # Step 4: Write output
    if output_path is None:
        output_path = elf_path.with_suffix(".addrinfo")
    else:
        output_path = Path(output_path)

    with open(output_path, "w") as f:
        json.dump(addrinfo, f, separators=(",", ":"))

    file_size = output_path.stat().st_size
    if verbose:
        print(f"Written {output_path} ({file_size:,} bytes, {len(entries)} symbols)")

    return output_path


def main():
    parser = argparse.ArgumentParser(
        description="Generate .addrinfo file from ESP32 ELF firmware"
    )
    parser.add_argument("elf_file", help="Path to the ELF firmware file")
    parser.add_argument(
        "-o", "--output", help="Output .addrinfo file path (default: <elf>.addrinfo)"
    )
    parser.add_argument(
        "--mcu",
        help="MCU type (esp32, esp32s3, etc.). Auto-detected if omitted.",
    )
    parser.add_argument("--build", help="Build variant name (wifi, bt, noradio)")
    parser.add_argument("--tag", help="Release tag (e.g. v4.0.0)")
    parser.add_argument(
        "--toolchain-prefix",
        help="Toolchain prefix (e.g. xtensa-esp32-elf-)",
    )
    parser.add_argument(
        "--no-shorten-paths",
        action="store_true",
        help="Don't shorten source file paths",
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    args = parser.parse_args()

    generate_addrinfo(
        elf_path=args.elf_file,
        output_path=args.output,
        mcu=args.mcu,
        build=args.build,
        tag=args.tag,
        toolchain_prefix=args.toolchain_prefix,
        shorten_paths=not args.no_shorten_paths,
        verbose=args.verbose,
    )


if __name__ == "__main__":
    main()
