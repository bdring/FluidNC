# Issues found while annotating config items

Found during the effort to annotate every `handler.item()` call in the FluidNC
config system with `@config`/`@default` comments (see `DECISIONS.md` /
`PROGRESS.md` for that effort's own log). Wiki pages that have drifted from
the actual firmware behavior, plus known scope boundaries of the generator
tooling. (A separate batch of code-quality issues found in passing was fixed
directly in source and is no longer listed here.)

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

## 2. Generator limitations (not bugs — known scope boundaries)

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
