# FluidNC Test Implementation Plan

This guide defines how to add tests consistently across unit, host integration, and fixture hardware suites.

## Suite Selection
- Unit (`tests` / `tests_coverage`):
  - pure logic/parsing/math/helpers
  - deterministic, no serial timing assumptions
- Host integration (`integration` / `integration_coverage`):
  - module interaction and state transitions
  - protocol/config/machine behavior with test stubs
  - suite isolation comes from PlatformIO discovery over top-level `FluidNC/tests/test_integration_*`
- Fixture hardware (`fixture_tests`):
  - serial/runtime behavior on real hardware profile
  - startup/reset/recovery flows and command sequencing

## Where To Add Tests
- Configuration parsing/runtime behavior:
  - `FluidNC/tests/test_integration_config/`
  - `FluidNC/tests/test_integration_config_runtime/`
- Machine/axes/homing behavior:
  - `FluidNC/tests/test_integration_machine/`
  - `FluidNC/tests/test_integration_machine_axes/`
  - `FluidNC/tests/test_integration_machine_buses/`
- Spindle protocol behavior:
  - `FluidNC/tests/test_integration_spindles/`
- Motion planner behavior:
  - `FluidNC/tests/test_integration_motion_planner/`
- Protocol command sequencing/realtime transitions:
  - `FluidNC/tests/test_integration_protocol/`
- Fixture controller/parser/upload semantics:
  - `fixture_tests/tests/test_op_entries.py`
- Hardware scenario fixtures:
  - `fixture_tests/fixtures/*.nc`

## Env Wiring Pattern (`platformio.ini`)
1. Add a suite directory under `FluidNC/tests/test_integration_<suite_name>/`.
2. Put suite-local test files in that directory and include `test_main.cpp`.
3. If helpers are shared across suites, place them in `FluidNC/tests/support/` or `FluidNC/capture/`, not in a discoverable suite directory.
4. Integration envs use one shared host build surface from `integration_common.build_src_filter`.
   Add product and capture sources there when they are part of the common stage surface.
5. Update the shared `integration_common` source/filter list in `platformio.ini` only when the new suite needs additional host-safe firmware or capture sources.
6. Prefer composition/bootstrap seams over product-file test branches:
   - keep hardware/API shims in `FluidNC/capture/`
   - move module registration or setting/bootstrap side effects into dedicated registration translation units
   - instantiate real product modules in tests whenever practical
7. Keep the integration build path-neutral:
   - `tools/integration_path_aliases.py` and the `g++`/`gcc`/`ar`/`ranlib` wrappers exist to make PlatformIO-relative paths resolvable from compiler working directories
   - add more source wrappers only if a specific compiler invocation still cannot be expressed through the shared surface
   - treat this as a compatibility bridge, not the preferred steady-state design
   - the longer-term cleanup would be a PlatformIO/layout change that emits stable absolute source, object, and archive paths without build-time rewriting
8. If the new suite should be skippable in coverage, add a matching `--skip-...` mapping in `coverage.py`.

## Verification Commands
- Unit:
  - `pio test -e tests -vv`
- Host integration:
  - `pio test -e integration -vv`
  - `pio test -e integration -f test_integration_<suite_name> -vv`
- Host integration coverage:
  - `pio test -e integration_coverage -vv`
- Host integration ASan:
  - `pio test -e integration_asan -f test_integration_machine_axes -vv`
- Fixture tool:
  - `python -m unittest discover -s fixture_tests/tests -v`
- Coverage:
  - `python coverage.py`

## Per-Suite Test Template
- Arrange:
  - initialize deterministic globals and stubs
  - reset shared state in helper functions
- Act:
  - execute one explicit operation path
- Assert:
  - verify state transition, emitted message/event, and error/alarm behavior
- Failure-path:
  - include at least one invalid input or timeout path

## CI Expectations
- Required checks:
  - `tests` / `tests_nosan`
  - one host integration job running `pio test -e integration`
  - fixture tool Python tests
  - coverage artifact generation

## Anti-Patterns To Avoid
- adding discoverable files under `FluidNC/tests/support/`
- broad integration source additions without validating they are host-safe under the shared env
- tests depending on timing races or implicit serial ordering
- mutable global state without reset helper
- changing coverage denominator to hide missing execution
- merging fixture scenarios without documenting required machine profile
- `PIO_UNIT_TESTING` behavior forks in product files when a composition-root or shim-based seam would work
- test-local reimplementations of product modules that make coverage appear better than the exercised code really is
- suite-local include-trampoline `.cpp` files when the shared host integration surface can compile the sources directly
- trying to replace the path bridge with more source wrappers instead of fixing the build graph or build paths
