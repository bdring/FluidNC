#!/usr/bin/env python3
"""Make an ASCII-only (32..126) copy of a ThingPulse SSD1306 font.

The ThingPulse "ESP8266 and ESP32 OLED driver for SSD1306 displays" fonts
cover characters 32..255.  FluidNC only draws ASCII and maps everything else
to '?', so the Latin-1 glyphs are dead weight.

Font layout: width, height, first char, number of chars, then 4 bytes per
char (offset MSB, offset LSB, byte size, advance width; offset 0xFFFF means
an empty glyph), then the glyph bitmaps in character order.  Keeping the
first N chars means keeping the first N jump-table entries and the bitmap
data up to the end of the last kept glyph.

Usage: trim_oled_font.py OLEDDisplayFonts.h FontName NewName > out.inc
"""

import re
import sys

LAST_CHAR = 126


def read_font(header, name):
    text = open(header, encoding="utf-8").read()
    m = re.search(r"const uint8_t " + re.escape(name) + r"\[\] PROGMEM = \{(.*?)\};", text, re.S)
    if not m:
        sys.exit(f"{name} not found in {header}")
    body = re.sub(r"//[^\n]*", "", m.group(1))
    return [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+", body)]


def trim(data):
    width, height, first, count = data[:4]
    keep = LAST_CHAR - first + 1
    if keep > count:
        sys.exit("font already ends before the last ASCII character")
    jump = data[4 : 4 + 4 * count]
    bitmaps = data[4 + 4 * count :]
    end = 0
    for i in range(keep):
        msb, lsb, size, _ = jump[4 * i : 4 * i + 4]
        if msb == 0xFF and lsb == 0xFF:
            continue
        end = max(end, (msb << 8 | lsb) + size)
    return [width, height, first, keep] + jump[: 4 * keep] + bitmaps[:end]


def emit(new_name, src_name, data):
    out = [f"// {src_name}, characters {data[2]}..{data[2] + data[3] - 1} only ({len(data)} bytes)"]
    out.append(f"const uint8_t {new_name}[] = {{")
    for i in range(0, len(data), 16):
        out.append("    " + ", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ",")
    out.append("};")
    return "\n".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    header, name, new_name = sys.argv[1:]
    print(emit(new_name, name, trim(read_font(header, name))))
