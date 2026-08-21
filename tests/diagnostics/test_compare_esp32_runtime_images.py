import hashlib
import importlib.util
import struct
from pathlib import Path

import pytest


TOOL = Path(__file__).parents[2] / "tools" / "compare_esp32_runtime_images.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("compare_esp32_runtime_images", TOOL)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def fake_esp32_image(*, runtime_byte=0x5A, elf_byte=0x11):
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = 1
    header[23] = 1
    segment = bytearray(200)
    struct.pack_into("<I", segment, 0, 0xABCD5432)
    segment[144:175] = bytes([elf_byte]) * 31
    segment[175] = 0xA7
    segment[180] = runtime_byte
    image = header + struct.pack("<II", 0x3F400020, len(segment)) + segment
    checksum_offset = ((len(image) + 15) // 16) * 16 - 1
    image.extend(bytes(checksum_offset + 1 - len(image)))
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    image[checksum_offset] = checksum
    image.extend(hashlib.sha256(image).digest())
    return bytes(image)


def test_self_referential_elf_and_footer_differences_are_runtime_equivalent(tmp_path):
    tool = load_tool()
    first = tmp_path / "first.bin"
    second = tmp_path / "second.bin"
    first.write_bytes(fake_esp32_image(elf_byte=0x11))
    second.write_bytes(fake_esp32_image(elf_byte=0x44))

    result = tool.compare_images(first, second)

    assert result["runtimeEquivalent"] is True
    assert result["differentBytes"] > 32
    assert result["unexpectedDifferenceOffsets"] == []
    assert result["normalizedSha256"]["first"] == result["normalizedSha256"]["second"]


def test_runtime_payload_difference_fails_closed(tmp_path):
    tool = load_tool()
    first = tmp_path / "first.bin"
    second = tmp_path / "second.bin"
    first.write_bytes(fake_esp32_image(runtime_byte=0x5A))
    second.write_bytes(fake_esp32_image(runtime_byte=0xA5))

    with pytest.raises(tool.ImageMismatch, match="runtime payload differs"):
        tool.compare_images(first, second)


def test_malformed_or_truncated_image_is_rejected(tmp_path):
    tool = load_tool()
    malformed = tmp_path / "malformed.bin"
    valid = tmp_path / "valid.bin"
    header = bytearray(24)
    header[0] = 0xE9
    header[1] = 1
    header[23] = 1
    malformed.write_bytes(header + struct.pack("<II", 0x3F400020, 4096))
    valid.write_bytes(fake_esp32_image())

    with pytest.raises(tool.ImageFormatError, match="truncated segment"):
        tool.compare_images(malformed, valid)


@pytest.mark.parametrize(
    ("corrupt_offset", "message"),
    [(-33, "checksum"), (-1, "validation SHA-256")],
)
def test_corrupt_checksum_or_validation_hash_is_rejected(tmp_path, corrupt_offset, message):
    tool = load_tool()
    valid = tmp_path / "valid.bin"
    corrupt = tmp_path / "corrupt.bin"
    image = fake_esp32_image()
    damaged = bytearray(image)
    damaged[corrupt_offset] ^= 0x01
    valid.write_bytes(image)
    corrupt.write_bytes(damaged)

    with pytest.raises(tool.ImageFormatError, match=message):
        tool.compare_images(valid, corrupt)


def test_image_without_appended_hash_flag_is_rejected(tmp_path):
    tool = load_tool()
    valid = tmp_path / "valid.bin"
    missing_flag = tmp_path / "missing-flag.bin"
    image = fake_esp32_image()
    damaged = bytearray(image)
    damaged[23] = 0
    valid.write_bytes(image)
    missing_flag.write_bytes(damaged)

    with pytest.raises(tool.ImageFormatError, match="hash_appended"):
        tool.compare_images(valid, missing_flag)
