# FluidNC Testing Guide

## Overview

FluidNC now includes a comprehensive regression test suite (370 tests) designed to catch regressions early and ensure code quality, especially important for an open source CNC firmware project where contributors frequently add features and fix bugs.

**Quick Start:**
```bash
pio test -e tests_nosan
```

## Optional: run tests automatically on local changes (Git hook)

This repo includes an **optional** `pre-commit` hook that runs the unit tests whenever you `git commit`.

- It is **non-blocking**: commits will still succeed if tests fail.
- If tests fail, it prints a clear warning so failures are caught early.

Enable it once per clone:

```bash
git config core.hooksPath .githooks
```

Skip the hook for a single commit:

```bash
FLUIDNC_SKIP_TESTS=1 git commit
```

Build time: ~12 seconds | All 370 tests pass

## Running Tests

### Run all tests
```bash
pio test -e tests_nosan
```

### Run with address sanitizer (catches memory bugs)
```bash
pio test -e tests
```

### Run specific test suite
```bash
pio test -e tests_nosan --filter StringUtilTest
pio test -e tests_nosan --filter ErrorTest
```

### Run with verbose output
```bash
pio test -e tests_nosan -vv
```

## Test Coverage

### Safety-Critical (129 tests)
**ErrorTest.cpp** (91 tests)
- All 91 Error enum values and their byte values
- Error code ranges (GCode, filesystem, expression, flow control)
- Ensures error codes remain consistent across versions

**StateTest.cpp** (24 tests)
- Machine state enum values (Idle, Alarm, ConfigAlarm, Critical, etc.)
- State transition safety constraints
- Safety-critical state validation

**RealtimeCmdTest.cpp** (14 tests)
- Realtime command codes (Reset, FeedHold, SafetyDoor, etc.)
- Override command sequences
- Ensures interrupt handling is reliable

### Feature Stability (241 tests)
**StringUtilTest.cpp** (69 tests)
- String comparison (case-insensitive)
- Trimming and whitespace handling
- Number parsing (hex, decimal, float)
- String splitting and manipulation

**UTF8Test.cpp** (59 tests)
- UTF-8 encoding/decoding for all byte lengths
- Round-trip verification (encode→decode)
- Edge cases and error handling
- Supports international character sets

**UtilityTest.cpp** (32 tests)
- Template utilities: `myConstrain`, `myMap`, `mapConstrain`
- Conversion constants (MM↔inch)
- MIN/MAX macros
- Floating-point tolerance handling

**RegexprTest.cpp** (72 tests)
- Pattern matching for setting names
- Wildcard handling (`*` for "any characters")
- Anchors (`^` start, `$` end)
- Case-sensitive and case-insensitive matching

### Existing Tests (7 tests)
**PinOptionsParserTest.cpp** (7 tests)
- Pin configuration parsing
- Option validation

## Why This Matters for Open Source

### Regression Prevention
When contributors add features or fix bugs, they can immediately see if they've broken core functionality:
- State machine logic
- Error handling
- String processing (used everywhere)
- Realtime commands (interrupt handling)

### Quick Feedback Loop
- 8-second build time means tests run during development
- Contributors get instant feedback on breaking changes
- Catches issues before code review

### Documentation Through Tests
Each test suite documents expected behavior:
- What values are valid?
- What happens in edge cases?
- How do components interact?

## Adding Tests

### When to add tests
- Adding a new feature → add tests for it
- Fixing a bug → add a test that catches the bug
- Modifying core utilities → verify edge cases

### Where to add tests
- **String/text processing** → `StringUtilTest.cpp`
- **Encoding/decoding** → `UTF8Test.cpp`
- **Math/templates** → `UtilityTest.cpp`
- **Pattern matching** → `RegexprTest.cpp`
- **Machine states** → `StateTest.cpp`
- **Error handling** → `ErrorTest.cpp`
- **Commands** → `RealtimeCmdTest.cpp`
- **New standalone module** → `ModuleNameTest.cpp`

### Test template
```cpp
#include <gtest/gtest.h>
#include "../src/YourModule.h"

TEST(YourTestSuite, DescriptiveTestName) {
    // Arrange
    int input = 5;
    
    // Act
    int result = your_function(input);
    
    // Assert
    EXPECT_EQ(result, 10);
}
```

### Floating-point tests
Use `EXPECT_NEAR` with appropriate tolerance:
```cpp
// Good - allows for FP rounding
EXPECT_NEAR(calculated_value, 1.0f, 0.0001f);

// Bad - too strict for IEEE 754 arithmetic
EXPECT_FLOAT_EQ(calculated_value, 1.0f);
```

### Enum value testing
Tests freeze enum values to catch regressions:
```cpp
TEST(StateEnum, IdleIsZero) {
    EXPECT_EQ(static_cast<uint8_t>(State::Idle), 0);
}
```

**Important:** Enum values are intentionally frozen. If you reorder enums, tests will fail. This is by design — it catches unintended changes and ensures backward compatibility. Document enum value changes in your PR.

### Edge cases and degenerate inputs
Test degenerate inputs (e.g., empty ranges, zero denominators):
```cpp
// Document expected behavior
TEST(MyFunction, DegenerateRange) {
    // Note: myMap(x, in_min, in_max, out_min, out_max) 
    // divides by (in_max - in_min).
    // Behavior is undefined when in_max == in_min.
    // This is acceptable; document it in your code.
}
```

## Test Architecture

### Dependencies
- **Google Test** (googletest @ ^1.15.2)
- No other external dependencies
- Standalone functions only (no hardware dependencies)

### Build Configuration
- **Platform**: PlatformIO native (x86/x64)
- **Standard**: C++17
- **Compiler**: G++ (MSYS2 UCRT64)

### Files Modified
- `platformio.ini`: Added build_src_filter to compile test modules
- No changes to production code

## Test Results

Current status:
- **380 tests** across 8 modules
- **100% passing**
- **~8 second** build time
- **Zero external dependencies** beyond googletest

### Recent test results
```
================ 380 test cases: 380 succeeded in 00:00:08.019 ================
```

## Troubleshooting

### Build errors with new tests
- Ensure test file is added to `platformio.ini` build_src_filter
- Check for circular dependencies or missing includes
- Verify only standalone functions are tested (no hardware dependencies)

### Tests fail after changes
- Expected if you modified core functions
- Use test output to identify what changed
- Update tests if new behavior is intentional

### Can't link external functions
Some functions have many dependencies. If linking is problematic:
- Test only the enum values
- Test only compile-time constants
- Use mocking for complex dependencies
- Consider if the function belongs in a separate, testable module

## Contributing

When submitting a PR:
1. Run tests: `pio test -e native`
2. If adding features, add corresponding tests
3. Ensure all 380+ tests still pass
4. New tests should increase coverage, not just add noise

This helps maintain code quality and makes future maintenance easier for the community.

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [PlatformIO Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- FluidNC Source: `FluidNC/src/`
- Tests: `FluidNC/tests/`
