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

### Code Coverage

To generate code coverage reports:

```bash
pip install gcovr
pio test -e tests_coverage
python coverage.py                    # Text report
python coverage.py --html             # Interactive HTML report
```

The coverage script works cross-platform (Windows, macOS, Linux) and generates reports showing line, branch, and function coverage for the tested source files.

## Test suites

The unit test environment (`-e tests`) builds a reduced subset of the firmware along with a set of GoogleTest suites.
Which sources are compiled is controlled by `platformio.ini` in the `[tests_common]` `build_src_filter`.

For coverage analysis, use `-e tests_coverage` which generates `.gcda` files that `gcovr` processes into detailed reports.

Current suites in `FluidNC/tests` include:

- `StringUtilTest.cpp` (string parsing/utilities)
- `UTF8Test.cpp` (UTF-8 encode/decode)
- `UtilityTest.cpp` (small template helpers / conversion constants)
- `RegexprTest.cpp` (FluidNC simplified matcher with `^`, `$`, `*` used for settings name matching)
- `RealtimeCmdTest.cpp` (locks down realtime command byte values and ordering/grouping invariants)
- `ErrorTest.cpp` (`Error` enum numeric values and grouping/range invariants)
- `ErrorBehaviorTest.cpp` (observable behavior around `ErrorNames` lookup)
- `StateTest.cpp` (`State` enum numeric values / ordering invariants)
- `CommandCompletionTest.cpp` (completion behavior via production `num_initial_matches()` against both NVS settings and config trees)

The completion test includes examples of both **settings-based** completion (non-`/` keys like `"sd/"`) 
and **config-tree** completion (keys starting with `/` like `"/axes/"`). Config-tree tests use a minimal 
`FakeConfigurable` to validate completion against a real config tree structure without initializing 
the full firmware.

## Making a unit test

Normally, if you make a new piece of code, you want to know that it's correct
and you want to ensure it *stays* correct. The latter is the main reason 
for unit tests; otherwise we should test the same features over and over 
again in the face of constant changes.

Making a test works as follows:

1. Create a new CPP file in the `FluidNC/tests` folder (this is the PlatformIO `test_dir`), probably in some sub-folder
2. The contents should be something like this:

```c++
#include "TestFramework.h"

#include <src/SomethingYouWantTested.h>

namespace Configuration {
    Test(TestCollectionName, TestName) {
        // doSomething...
        Assert(myCondition, "Description; test fails if !condition");

        // and do this for all functionalities exposed
    }
}

```

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
