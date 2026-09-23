#!/usr/bin/env python3
"""Host regression for notifyf's two-pass printf formatting; no hardware access.

Build the exact notifyf definition from Report.cpp with a recording notify sink.
The rest of Report.cpp depends on the full controller; this isolated test covers
the formatter's varargs and stack/heap buffer branches only. Requires g++.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    source = (args.source_root / "FluidNC/src/Report.cpp").read_text()
    start = source.index("void notifyf(")
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    function = source[start:end]
    harness = '''#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
std::string actual_title, actual_message;
void notify(const char* title, const char* message) {
    actual_title = title; actual_message = message;
}
'''+function+'''
int main() {
    notifyf("Job done", "%s job sent", "after_reset");
    assert(actual_title == "Job done" && actual_message == "after_reset job sent");
    notifyf("Mixed", "%s:%d/%.3f", "tool", 7, 1.25);
    assert(actual_message == "tool:7/1.250");
    notifyf("Empty", "%s", ""); assert(actual_message.empty());
    for (size_t length : {size_t(63), size_t(64), size_t(200)}) {
        std::string input(length, 'x');
        notifyf("Boundary", "%s", input.c_str()); assert(actual_message == input);
    }
    puts("PASS: notifyf short/mixed/empty/63/64/200-byte messages");
}
'''
    with tempfile.TemporaryDirectory(prefix="fluidnc-notifyf-") as directory:
        root = Path(directory)
        (root / "test.cpp").write_text(harness)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(root / "test.cpp"),
                        "-o", str(root / "test")], check=True)
        subprocess.run([str(root / "test")], check=True, timeout=10)


if __name__ == "__main__":
    main()
