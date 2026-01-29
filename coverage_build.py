"""
PlatformIO extra script to properly configure coverage for native tests.
Ensures gcov library is linked for coverage data collection.

Note: GoogleTest library will also be instrumented with coverage, which adds
some noise to coverage reports. Filtering is handled by gcovr exclusion patterns
in coverage.py to focus on FluidNC source files only.
"""
Import("env")

# Add gcov library to linker for coverage data collection
env.Append(LIBS=["gcov"])
