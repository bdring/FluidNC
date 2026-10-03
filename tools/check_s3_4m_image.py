#!/usr/bin/env python3
"""Check the built 4 MB S3 image, including its embedded partition table."""

import argparse
import hashlib
from pathlib import Path
import struct


def check(build_dir):
    flash_size = 0x400000
    table = (build_dir / "partitions.bin").read_bytes()
    partitions = []
    for pos in range(0, len(table), 32):
        row = table[pos:pos + 32]
        if row[:2] == b"\xeb\xeb":
            if row[16:] != hashlib.md5(table[:pos]).digest():
                raise ValueError("Partition table MD5 mismatch")
            break
        if row[:2] != b"\xaa\x50" or len(row) != 32:
            raise ValueError("Invalid partition table entry or missing MD5")
        _, kind, subtype, offset, size, label, _ = struct.unpack("<HBBII16sI", row)
        partitions.append((kind, subtype, offset, size, label.rstrip(b"\0").decode()))
    else:
        raise ValueError("Missing partition table MD5")

    previous_end = 0x9000
    for _, _, offset, size, label in sorted(partitions, key=lambda p: p[2]):
        if size == 0 or offset < previous_end or offset + size > flash_size:
            raise ValueError(f"Invalid partition bounds: {label} at {offset:#x}, size {size:#x}")
        previous_end = offset + size
        print(f"{label}: {offset:#08x}..{previous_end:#08x}")

    apps = [p for p in partitions if p[0] == 0]
    filesystems = [p for p in partitions if p[:2] == (1, 0x82)]
    if len(apps) != 1 or apps[0][1] != 0 or len(filesystems) != 1:
        raise ValueError("Expected one factory application, no OTA slots, and one LittleFS partition")

    merged = (build_dir / "merged-flash.bin").read_bytes()
    if len(merged) > flash_size:
        raise ValueError("Merged image exceeds 4 MB")
    if merged[0] != 0xe9 or struct.unpack_from("<H", merged, 12)[0] != 9 or merged[3] >> 4 != 2:
        raise ValueError("Bootloader header must specify ESP32-S3 and 4 MB flash")
    if merged[0x8000:0x8000 + len(table)] != table:
        raise ValueError("Merged image contains a different partition table")

    for name, partition in (("firmware.bin", apps[0]), ("littlefs.bin", filesystems[0])):
        data = (build_dir / name).read_bytes()
        offset, capacity = partition[2:4]
        if not data or len(data) > capacity:
            raise ValueError(f"{name} does not fit its partition")
        if merged[offset:offset + len(data)] != data:
            raise ValueError(f"{name} is missing or at the wrong offset in the merged image")
        print(f"{name}: {len(data):,} / {capacity:,} bytes")
    print("4 MB ESP32-S3 merged image OK")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    args = parser.parse_args()
    try:
        check(args.build_dir)
    except (ValueError, OSError, struct.error, IndexError) as error:
        parser.exit(1, f"Image check failed: {error}\n")
