# Issues found while annotating config items

Found during the effort to annotate every `handler.item()` call in the FluidNC
config system with `@config`/`@default` comments (see `DECISIONS.md` /
`PROGRESS.md` for that effort's own log). Two categories: wiki pages that
have drifted from the actual firmware behavior, and code-quality problems
spotted in passing. Nothing here was fixed as part of the annotation work
itself — these are handoff notes.

## 1. Wiki pages that have drifted from source

In every case below, the FluidNC source is correct and the wiki
(wiki.fluidnc.com) is wrong. The `@config`/`@default` annotations already
written into the source reflect the *source* value, not the wiki's.

| Wiki page | Item | Wiki says | Source actually says | Source location |
|---|---|---|---|---|
| Top Level Config Items | `verbose_errors` | default `false` | default `true` | `FluidNC/src/Machine/MachineConfig.h:125` |
| Top Level Config Items | `board:` / `name:` | default empty string, 80-char limit | default `"None"`, 255-char limit (the `item()` call's own default — no explicit limit is passed) | `FluidNC/src/Machine/MachineConfig.h:139-140` |
| Parking Feature | `parking.enable` | default `true` | default `false` | `FluidNC/src/Parking.h:17` |
| Axes | `homing.cycle` | default `-1` | default `0` | `FluidNC/src/Machine/Homing.h:55` |
| Axes | `homing.mpos_mm` | range constrained to ±1,000,000 | unbounded — the `item()` call passes no min/max at all | `FluidNC/src/Machine/Homing.h` (`handler.item("mpos_mm", _mpos);`) |
| Axes | `max_rate_mm_per_min` | range tops out at 100000 | range tops out at 250000 | `FluidNC/src/Machine/Axis.cpp:22` |
| Spindles (config_spindles) | `spinup_ms` / `spindown_ms` | range tops out at 20000 | range tops out at 60000 | `FluidNC/src/Spindles/Spindle.h:109,114` |
| Spindles (config_spindles) | `tool_num` | `Type: Pin` | `Type: Integer` | `FluidNC/src/Spindles/Spindle.h:124` |
| Spindles (config_spindles) | `s0_with_disable` | default `true` | default `false` | `FluidNC/src/Spindles/Spindle.h:30,155` |
| UART Sections | `mode:` (on a `uartN:` section) | default written as `"N81"` | actual default is `"8N1"` (8 data bits, no parity, 1 stop bit) — the wiki's text is garbled/transposed | `FluidNC/src/Uart.h:45-47` |
| UART Sections | `uart_channelN.report_interval_ms` | range constrained to 50-5000 | unbounded — the `item()` call passes no min/max at all | `FluidNC/src/UartChannel.h:16,66` |
| Status Outputs | `run_pin` / `hold_pin` / `alarm_pin` / `door_pin` | labeled `Type: Pin (input)` | these are outputs the firmware drives based on machine state (e.g. to run a stack light), not inputs | `FluidNC/src/Status_outputs.h:8-12` |
| Modbus VFD Spindles | `uart_num` | default `1` | default `-1` (meaning "not configured") | `FluidNC/src/Spindles/VFDSpindle.h:35` |

## 2. Code-quality issues found in passing

These were flagged as separate background tasks rather than fixed inline,
since they're out of scope for a documentation pass.

### 2.1 Dead shadow field in `OnOffSpindle.h` — fix in progress

`Spindles::Spindle` (base class) has a **private** field:
```cpp
bool _zero_speed_with_disable = false;   // Spindle.h:30
```
registered as the config item `s0_with_disable` (`Spindle.h:155`) and read in
`Spindle.cpp:162`.

`Spindles::OnOff` (a subclass) separately declares its **own** field with the
exact same name but a different default:
```cpp
bool _zero_speed_with_disable = true;    // OnOffSpindle.h:61 (protected)
```
Because the base class's field is `private`, this isn't an override — it's a
completely independent variable that happens to share a name. Confirmed via
whole-tree `grep` that `OnOff`'s copy is never read anywhere except its own
declaration. It looks load-bearing (same name, same comment, copied from the
real field) but does nothing.

**Status:** flagged as task `task_a0a53682` ("Remove dead
`_zero_speed_with_disable` shadow in `OnOffSpindle.h`") — you've started this
one in a separate session; it's running now.

### 2.2 Three Kinematics findings — bundled, not yet started

Flagged as task `task_564db26f` ("Audit dead/broken fields in Kinematics/").

- **`CoreXY::group()` / `Midtbot::group()` are both empty** (`CoreXY.cpp`,
  `Midtbot.cpp`), yet `CoreXY.cpp`'s own header comment (lines 11-26)
  advertises a working `x_scaler: 1` config example, and `_x_scaler` is a
  real field actively used in the kinematics math
  (`transform_cartesian_to_motors`/`motors_to_cartesian`). It is **not**
  actually configurable from `config.yaml` at all — setting it in a real
  config file would be silently ignored as an unrecognized key. Matches the
  file's own "TODO: Implement scalers" comment, so this is known-incomplete,
  not a fresh regression. `Midtbot.cpp` instead hardcodes `_x_scaler = 2.0;`
  in `init()`.
- **`ParallelDelta.h:76,78`**: `_homing_degrees` (default `0.0`) and
  `_down_degrees` (default `90.0`) are declared with real-looking defaults,
  but neither is read anywhere in the codebase, nor registered via
  `handler.item()` in `ParallelDelta::group()` — which does register the
  seemingly-paired `up_degrees`. Both fields are dead.
- **`WallPlotter.cpp`**: `WallPlotter::transform_cartesian_to_motors()`
  contains `log_error("WallPlotter::transform_cartesian_to_motors is
  broken");` — the kinematics system apparently self-reports as
  non-functional. Worth checking whether this is still accurate or a stale
  warning from a since-fixed implementation.

## 3. Generator limitations (not bugs — known scope boundaries)

Not "problems" in the same sense as the above, but worth knowing if you're
about to run `tools/gen_config_docs.py` yourself:

- An `EnumItem` array defined in a different `.cpp` than the one being
  parsed (e.g. `messageLevels2` in `Logging.cpp`, referenced from
  `UartChannel.h`; `phyTypes` for `EthPhy.h`) isn't resolved when the
  generator runs against a single file — it reports the field's raw
  underlying type instead of `enum`. The tool is single-file-per-invocation
  by design; whole-tree resolution is future work.
- Nested member access after array indexing (`I2CPinExtenderBase`'s
  `_isrData[i]._pin`) reports `type: unknown` for the same reason.
- Three "data-driven item list" shapes — `Control.cpp`/`UserInputs.cpp`'s
  loop-over-a-named-array, `Macros.cpp`'s constructor-registration, and
  `RGBLed.h`'s `handleRGBString()` wrapper — are annotated by hand per the
  documented exception in `FluidNC/src/Configuration/ItemDocs.md`, but the
  generator can't parse any of them mechanically yet.
