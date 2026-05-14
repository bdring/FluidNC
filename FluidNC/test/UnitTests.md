# Unit tests

## Getting started

Getting started with unit tests:

1. Install G++/GCC. For Windows, I recommend MSYS2 (https://www.msys2.org/).
   Make sure G++ is in the search path. This has been tested on gcc
   version 11.2.0, which is the default for GCC at the time of writing.
2. Test G++. `g++ -v` from a command prompt should do the trick
3. Run all unit tests. 

Running unit tests is done by:

`pio test -e tests`

Tip: you can add `-v` (or `-vv`) to see the exact compile/link steps when diagnosing build issues.

### Pre-commit Hook

The repository includes a pre-commit hook that automatically runs `pio test -e tests` before allowing commits.
This ensures all tests pass before changes enter the repository.

To temporarily bypass the hook (e.g., for work-in-progress commits):

```powershell
# PowerShell (Windows)
$env:FLUIDNC_SKIP_TESTS=1; git commit -m "WIP: ..."
```

```bash
# Bash (Linux/macOS)
FLUIDNC_SKIP_TESTS=1 git commit -m "WIP: ..."
```

### Code Coverage

FluidNC provides two test environments:

- **`tests`**: Fast test runs (~8 seconds) without coverage instrumentation. Use for development and pre-commit validation.
- **`tests_coverage`**: Instrumented build with `--coverage` flags. Slower, but generates `.gcda` files for coverage analysis.

To generate code coverage reports:

```bash
pip install gcovr
python coverage.py                    # Runs tests_coverage + text report
python coverage.py --html             # Runs tests_coverage + interactive HTML report
```

The `coverage.py` script:
1. Cleans previous coverage data
2. Runs `pio test -e tests_coverage`
3. Uses `gcovr` to generate reports showing line, branch, and function coverage

Works cross-platform (Windows with MSYS2/MinGW, macOS, Linux).

## Test suites

The unit test environment (`-e tests`) builds a **minimal subset** of FluidNC sources along with GoogleTest suites.
This is NOT a complete firmware build - it focuses on testable utility code (string parsing, UTF-8, regex, error handling).

Which sources are compiled is controlled by `platformio.ini` in the `[tests_common]` `build_src_filter`.
Most firmware modules have tight coupling to FreeRTOS, hardware abstractions, and system globals - these are excluded from unit tests.

For coverage analysis, use `-e tests_coverage` which adds `--coverage` flags and generates `.gcda` files for `gcovr`.

Current suites in `FluidNC/tests` include:

- `StringUtilTest.cpp` (69 tests, 98.8% coverage - string parsing/utilities)
- `UTF8Test.cpp` (59 tests, 95.4% coverage - UTF-8 encode/decode)
- `RegexprTest.cpp` (72 tests, 100% coverage - simplified regex matcher with `^`, `$`, `*` for settings)
- `UtilityTest.cpp` (32 tests - template helpers/conversion constants)
- `ErrorBehaviorTest.cpp` (9 tests - observable behavior around `ErrorNames` lookup)
- `StateTest.cpp` (7 tests - `State` enum values/ordering invariants)
- `CommandCompletionTest.cpp` (10 tests, 85.4% coverage - tab completion for settings and config trees)
- `FluidErrorTest.cpp` (5 tests, 100% coverage - std::error_code integration for FluidError enum)
- `PinOptionsParserTest.cpp` (7 tests, 100% coverage - pin option string parsing)

Note: `RealtimeCmdTest.cpp` and `ErrorTest.cpp` test compile-time enum values and are not included in coverage metrics.

The test suite achieves **88.1% line coverage** on 293 lines across 11 source files (as of 2026-01-28).
### What's Tested vs. What's Not Tested

**Currently Tested (Pure Utility Code):**
- ✅ String parsing and manipulation (`string_util.cpp`)
- ✅ UTF-8 encoding/decoding (`UTF8.cpp`)
- ✅ Pattern matching (`Regexpr.cpp`)
- ✅ Error categorization (`FluidError.cpp`, `Error.cpp`)
- ✅ Pin option parsing (`PinOptionsParser.cpp`)
- ✅ Command completion logic (`Completer.cpp`)
- ✅ Template utilities (constrain, map, conversion constants)

**Not Tested (Hardware/RTOS Dependencies):**
- ❌ Motion control and stepper logic (requires FreeRTOS, hardware timers)
- ❌ SD card and filesystem operations (requires SPIFFS/LittleFS)
- ❌ WiFi, Bluetooth, Telnet (requires ESP32 radio hardware)
- ❌ I2C/SPI/UART drivers (requires hardware peripherals)
- ❌ GCode interpreter runtime (requires system globals, protocol state)
- ❌ Settings persistence (requires NVS flash access)

**Why This Approach?**

FluidNC is firmware with tight hardware coupling. Rather than building extensive mocking frameworks
(which would be brittle and maintenance-heavy), we focus on:

1. **Pure utilities** that can be tested in isolation
2. **Logic separation** - extracting testable algorithms from hardware layers
3. **Fast feedback** - 8-second test runs encourage frequent execution

For hardware-dependent features, prefer:
- Integration tests on real hardware
- Fixture tests (see `fixture_tests/` directory)
- Manual testing with representative configurations
## Making a unit test

Normally, if you make a new piece of code, you want to know that it's correct
and you want to ensure it *stays* correct. The latter is the main reason 
for unit tests; otherwise we should test the same features over and over 
again in the face of constant changes.

Making a test works as follows:

1. Create a new CPP file in the `FluidNC/tests` folder (this is the PlatformIO `test_dir`)
2. The contents should be something like this:

```c++
#include "gtest/gtest.h"
#include <SomethingYouWantTested.h>

namespace {

TEST(TestSuiteName, TestName) {
    // Arrange - set up test data
    int expected = 42;

    // Act - call the function under test
    int actual = myFunction();

    // Assert - verify the result
    EXPECT_EQ(actual, expected);
}

TEST(TestSuiteName, AnotherTest) {
    // Add more tests...
    EXPECT_TRUE(someCondition());
    EXPECT_FLOAT_EQ(3.14f, calculatePi(), 0.01f);
}

}  // namespace
```

3. Add the test file to `platformio.ini` in the `[tests_common]` `build_src_filter`
4. If your code depends on source files, add those to `build_src_filter` as well

It's that easy. Once done, it should work with the unit test environment.

## A note on testing "production" code in the native unit test build

The `tests` environment is intentionally a **reduced** build. The goal is fast feedback and stable, cross-platform unit tests.

Some modules (e.g. completion) can be tested by compiling the real production source file and providing only small stubs for the missing firmware runtime.
For example, `CommandCompletionTest.cpp` exercises the production `num_initial_matches()` implementation (built from `src/Configuration/Completer.cpp`).

Other modules (notably `Settings.cpp`, protocol/RTOS plumbing, and runtime globals) pull in a large dependency graph.
Trying to compile those directly into `tests` can easily turn into a full firmware build and/or a long chain of missing-symbol link errors.

When that happens, prefer adding a separate, slower **integration** test environment rather than bloating `tests`.

# Google test code and unity tests

Google tests are the go-to tests for cross-platform testing, while 
Unity tests are the default on ESP32. Both have their advantages; for
example, Google Tests have great support for IDE integration.

We made the test framework in such a way that it switches the test 
framework being used depending on the platform. So, ESP32 run natively
on Unity, and the rest runs on GTest. 

These unit tests are designed to test a portion of the FluidNC
code, directly from your desktop PC. This is not a complete test of 
FluidNC, but a starting point from which we can move on. Testing and 
debugging on a desktop machine is obviously much more convenient than 
it is on an ESP32 with a multitude of different configurations, not to
mention the fact that you can use a large variety of tools such as 
code coverage, profiling, and so forth.

Code here is split into two parts:
1. A subset of the GRBL code is compiled. Over time, this will become more.
2. Unit tests are executed on this code.

## Folders and how this works

Support libraries are implemented that sort-of mimic the Arduino API where
appropriate. This functionality might be extended in the future, and is by 
no means intended to be or a complete or even a "working" version; it's 
designed to be _testable_.

Generally speaking that means that most features are simply not available. 
Things like GPIO just put stuff in a buffer, things like pins can be logged
for analysis and so forth. 

The "Support" folder is the main thing that gives this mimicking ability,
so that the code in the FluidNC folder is able to compile. For example,
when including `<Arduino.h>`, in fact `X86TestSupport/TestSupport/Arduino.h` is included.

The include folders that have to be passed to the x86/x64 compiler are:

- X86TestSupport
- ..\FluidNC

Unfortunately, not everything can be tested properly in an easy way. For 
example, it is very hard to test ESP32 DMA and low-level things. External 
libraries are also very nasty to test. In some cases we want to exclude 
portions of the source code. This can be done in PlatformIO.INI, in the
`src_filter` property of the test environment. Take note: if you add things
in an env section in PIO, it *must* be supported by the targets that build it.

## Test code

Unit tests can be found in the `FluidNC/tests` folder.

## Troubleshooting

### Common Issues on Windows

**Problem: `g++: command not found` or tests fail to compile**

Solution: Install MSYS2 and add GCC to PATH:
1. Download and install MSYS2 from https://www.msys2.org/
2. Open MSYS2 MinGW 64-bit terminal
3. Install GCC: `pacman -S mingw-w64-x86_64-gcc`
4. Add to PATH: `C:\msys64\mingw64\bin` (adjust path if needed)
5. Verify: `g++ -v` should show version 11.2.0 or later

**Problem: Tests pass but coverage.py fails with "gcovr: command not found"**

Solution: Install gcovr via pip:
```bash
pip install gcovr
```

**Problem: "Permission denied" when running tests**

Solution: Close any IDEs or debuggers that might be locking test executables, then try again.

**Problem: Compilation errors about missing headers**

Solution: Ensure you're using `-e tests` (not the old `windows_x86` environment). Run:
```bash
pio test -e tests -vv
```
The `-vv` flag shows detailed compilation commands for debugging.

### Common Issues on macOS/Linux

**Problem: Tests fail with linker errors about gcov**

Solution: Ensure you have GCC (not just Clang) installed:
```bash
# macOS
brew install gcc

# Ubuntu/Debian
sudo apt-get install gcc g++
```

**Problem: Coverage reports are empty**

Solution: Verify `.gcda` files were generated:
```bash
find .pio/build/tests_coverage -name "*.gcda"
```
If empty, rebuild with: `pio test -e tests_coverage --verbose`

# Compiler details

Unit tests are currently running on 3 different platforms:

- Native x86 GCC/G++ through PlatformIO
- ESP32 GCC/G++ through PlatformIO
- MSVC++ through Visual Studio

## What's with -std=c++17

Well, apparently some of the features that the ESP32 compiler supports are 
actually C++17. In other words, you need a compiler capable of this syntax,
for the code to work.

All modern C++ Compilers support C++17. If yours doesn't, it's time for a
serious upgrade anyways, because it's definitely EOL.
