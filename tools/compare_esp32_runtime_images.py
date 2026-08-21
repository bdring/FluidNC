#!/usr/bin/env python3
"""Compare ESP32 application images while isolating self-referential hashes.

The ESP32 image embeds the SHA-256 of its source ELF in ``esp_app_desc_t``.
That value, the image checksum, and the trailing validation hash can differ
between otherwise byte-identical runtime images.  This tool parses the image
layout and fails closed if any byte outside those fields differs.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any


ESP_IMAGE_MAGIC = 0xE9
ESP_APP_DESC_MAGIC = 0xABCD5432
IMAGE_HEADER_BYTES = 24
SEGMENT_HEADER_BYTES = 8
APP_DESC_ELF_SHA_OFFSET = 144
SHA256_BYTES = 32


class ImageFormatError(ValueError):
    pass


class ImageMismatch(AssertionError):
    pass


def _sha256(data: bytes | bytearray) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def parse_layout(data: bytes) -> dict[str, Any]:
    if len(data) < IMAGE_HEADER_BYTES:
        raise ImageFormatError("truncated ESP32 image header")
    if data[0] != ESP_IMAGE_MAGIC:
        raise ImageFormatError(f"unexpected ESP32 image magic 0x{data[0]:02X}")
    segment_count = data[1]
    if segment_count < 1 or segment_count > 16:
        raise ImageFormatError(f"invalid ESP32 segment count {segment_count}")
    if data[23] != 1:
        raise ImageFormatError(f"ESP32 hash_appended flag must be 1, got {data[23]}")

    position = IMAGE_HEADER_BYTES
    segments = []
    for index in range(segment_count):
        if position + SEGMENT_HEADER_BYTES > len(data):
            raise ImageFormatError(f"truncated segment {index} header")
        load_address, length = struct.unpack_from("<II", data, position)
        data_offset = position + SEGMENT_HEADER_BYTES
        end_offset = data_offset + length
        if end_offset > len(data):
            raise ImageFormatError(
                f"truncated segment {index}: declares {length} bytes at 0x{data_offset:X}, image has {len(data)}"
            )
        segments.append(
            {
                "index": index,
                "loadAddress": load_address,
                "dataOffset": data_offset,
                "length": length,
                "endOffset": end_offset,
            }
        )
        position = end_offset

    # ESP images pad after the final segment and place the one-byte checksum at
    # the last byte of the next 16-byte block, followed by a 32-byte SHA-256.
    checksum_offset = ((position + 16) // 16) * 16 - 1
    validation_hash_offset = checksum_offset + 1
    expected_size = validation_hash_offset + SHA256_BYTES
    if expected_size != len(data):
        raise ImageFormatError(
            f"unexpected ESP32 footer layout: parsed end 0x{position:X}, expected {expected_size} bytes, got {len(data)}"
        )

    computed_checksum = 0xEF
    for segment in segments:
        for value in data[segment["dataOffset"] : segment["endOffset"]]:
            computed_checksum ^= value
    stored_checksum = data[checksum_offset]
    if stored_checksum != computed_checksum:
        raise ImageFormatError(
            f"invalid ESP32 segment checksum at 0x{checksum_offset:X}: "
            f"stored 0x{stored_checksum:02X}, computed 0x{computed_checksum:02X}"
        )

    stored_validation_hash = data[validation_hash_offset:expected_size]
    computed_validation_hash = hashlib.sha256(data[:validation_hash_offset]).digest()
    if stored_validation_hash != computed_validation_hash:
        raise ImageFormatError(
            "invalid ESP32 validation SHA-256: "
            f"stored {stored_validation_hash.hex().upper()}, computed {computed_validation_hash.hex().upper()}"
        )

    first_segment = segments[0]
    app_desc_offset = first_segment["dataOffset"]
    if first_segment["length"] < APP_DESC_ELF_SHA_OFFSET + SHA256_BYTES:
        raise ImageFormatError("first segment is too small for esp_app_desc_t")
    app_magic = struct.unpack_from("<I", data, app_desc_offset)[0]
    if app_magic != ESP_APP_DESC_MAGIC:
        raise ImageFormatError(f"missing esp_app_desc_t magic at 0x{app_desc_offset:X}")
    elf_sha_offset = app_desc_offset + APP_DESC_ELF_SHA_OFFSET

    return {
        "bytes": len(data),
        "segments": segments,
        "elfShaOffset": elf_sha_offset,
        "checksumOffset": checksum_offset,
        "validationHashOffset": validation_hash_offset,
    }


def _ignored_offsets(layout: dict[str, Any]) -> set[int]:
    return set(range(layout["elfShaOffset"], layout["elfShaOffset"] + SHA256_BYTES)) | {
        layout["checksumOffset"]
    } | set(range(layout["validationHashOffset"], layout["validationHashOffset"] + SHA256_BYTES))


def compare_images(first_path: Path, second_path: Path) -> dict[str, Any]:
    first = first_path.read_bytes()
    second = second_path.read_bytes()
    first_layout = parse_layout(first)
    second_layout = parse_layout(second)
    if first_layout != second_layout:
        raise ImageMismatch("ESP32 image layouts differ")

    ignored = _ignored_offsets(first_layout)
    differences = [index for index, (left, right) in enumerate(zip(first, second)) if left != right]
    unexpected = [index for index in differences if index not in ignored]
    if unexpected:
        sample = ", ".join(f"0x{offset:X}" for offset in unexpected[:16])
        raise ImageMismatch(
            f"runtime payload differs at {len(unexpected)} byte(s); first offsets: {sample}"
        )

    normalized_first = bytearray(first)
    normalized_second = bytearray(second)
    for offset in ignored:
        normalized_first[offset] = 0
        normalized_second[offset] = 0
    if normalized_first != normalized_second:
        raise ImageMismatch("normalized runtime images differ")

    elf_offset = first_layout["elfShaOffset"]
    validation_offset = first_layout["validationHashOffset"]
    return {
        "runtimeEquivalent": True,
        "bytes": len(first),
        "differentBytes": len(differences),
        "unexpectedDifferenceOffsets": unexpected,
        "ignoredFields": {
            "embeddedElfSha256": [elf_offset, elf_offset + SHA256_BYTES - 1],
            "imageChecksum": [first_layout["checksumOffset"], first_layout["checksumOffset"]],
            "validationSha256": [validation_offset, validation_offset + SHA256_BYTES - 1],
        },
        "sha256": {"first": _sha256(first), "second": _sha256(second)},
        "embeddedElfSha256": {
            "first": first[elf_offset : elf_offset + SHA256_BYTES].hex().upper(),
            "second": second[elf_offset : elf_offset + SHA256_BYTES].hex().upper(),
        },
        "normalizedSha256": {
            "first": _sha256(normalized_first),
            "second": _sha256(normalized_second),
        },
        "segments": first_layout["segments"],
    }


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        result = compare_images(args.first, args.second)
    except (OSError, ImageFormatError, ImageMismatch) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "PASS: runtime-equivalent ESP32 images; "
            f"{result['differentBytes']} self-referential byte differences; "
            f"normalized SHA-256 {result['normalizedSha256']['first']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
