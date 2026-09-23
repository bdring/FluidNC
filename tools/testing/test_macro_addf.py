#!/usr/bin/env python3
"""Compile the real Macro.h against a minimal Channel forward declaration.

Exercises generated G-code and append separators without controller hardware.
Requires Python 3 and g++. The macro execution engine is outside this test.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="fluidnc-macro-") as directory:
        root = Path(directory)
        shutil.copyfile(args.source_root / "FluidNC/src/Macro.h", root / "Macro.h")
        (root / "Channel.h").write_text('#pragma once\n#include <string>\n#include <string_view>\n#include <cstdio>\nclass Channel;\n')
        (root / "test.cpp").write_text('''#include "Macro.h"
#include <cassert>
int main() {
    Macro macro;
    macro.addf("G53G0Z%0.3f", 12.5);
    assert(macro.get() == "G53G0Z12.500");
    macro.addf("G53G0X%.3fY%.3fZ%.3f", -1.25, 2.5, 100.0);
    assert(macro.get() == "G53G0Z12.500&G53G0X-1.250Y2.500Z100.000");
    macro.erase(); macro.addf("(MSG,Install tool #%d: %s)", 7, "endmill");
    assert(macro.get() == "(MSG,Install tool #7: endmill)");
    macro.erase(); macro.addf("%s", ""); assert(macro.get().empty());
    std::string long_text(200, 'x'); macro.addf("%s", long_text.c_str());
    assert(macro.get() == long_text);
    macro.addf("M0"); assert(macro.get() == long_text + "&M0");
    puts("PASS: Macro::addf coordinates/mixed/empty/long/append");
}
''')
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-I", str(root),
                        str(root / "test.cpp"), "-o", str(root / "test")], check=True)
        subprocess.run([str(root / "test")], check=True, timeout=10)


if __name__ == "__main__":
    main()
