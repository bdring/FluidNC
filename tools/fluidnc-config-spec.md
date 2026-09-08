# FluidNC `config.yaml` Formal Specification (for LLM Generation)

> **Read this document together with [`FluidNC/docs/config_items.yaml`](https://github.com/bdring/FluidNC/blob/main/FluidNC/docs/config_items.yaml).**
> That file — generated directly from the FluidNC source annotations, so it never drifts — is the **authoritative, complete list of every config section and field**, with each field's type, range, default, and a description. This document does **not** repeat those per-field tables. What it covers instead: the file **grammar** (§0, §2, §3), whole-document and cross-field rules that a per-field list cannot express (§4), the common mistakes LLMs make when generating this format (§5), and full worked examples (§6). **This document alone is not sufficient to generate a correct config — you need `config_items.yaml` for the field details.** If the two ever disagree, `config_items.yaml` wins.
>
> **How to read `config_items.yaml`:**
> - Top-level keys are section *paths*, written with `.` separators and placeholder segments, e.g. `axes.<letter>.motorN.tmc_2130:`. In an actual `config.yaml` this becomes nesting: `axes:` → an axis letter (`x:`) → `motor0:` → `tmc_2130:`. `(top-level machine items):` holds the bare root-level scalars (`board`, `name`, …).
> - Placeholders: `<letter>` is an axis letter `x y z a b c u v w`; a trailing `N` (`motorN`, `uartN`, `i2cN`) is a single digit (`motor0`/`motor1`; `uart0`…`uart9`); `<i2c_chip>` is one of the chip type names. The exact regex for each is in the file's `section_meta:` block.
> - `type:` values map to §2 here: `pin` → §3 pin grammar; `enum` lists its `values:`; `uart_mode` → an `8N1`-style string; `std::vector<Configuration::speedEntry>` → a Speed Map (§2); `std::vector<float>` → a whitespace-separated float list (§2); `macro` → a single macro line (§4); everything else is a plain string, integer, float, or boolean.
> - `pin_namespaces:`, `spindle_sections:`, `vfd_named_types:`, `section_meta:` list the valid pin prefixes, spindle-type keys, named-VFD keys, and section repeat patterns.

**Purpose:** This document gives an LLM the file-format knowledge it needs to generate a syntactically and structurally correct FluidNC configuration file, and to avoid the specific mistakes that are common when generating YAML from a language model. It intentionally does **not** validate board-specific pin legality (e.g. "is GPIO 6 usable on this particular board") — pin identifiers are treated as an opaque grammar (see §3), not a board-specific lookup table. FluidNC's primary target is the ESP32, but a development build also runs on RP2040 — nothing here assumes ESP32-specific hardware unless explicitly noted (the one confirmed exception is DAC-type spindle output, which relies on an ESP32-specific peripheral).

**Ground truth.** Every config section corresponds to a `handler.section(name, ...)` call in the FluidNC C++ source, and every field to a `handler.item(name, ...)` call; the `// @config` annotation next to each `item()` call is what `tools/build_config_docs.py` extracts into `config_items.yaml`. The rules in this document were likewise verified against that source (`Configuration/Tokenizer.cpp`, `Configuration/Parser.cpp`, `Pin.cpp`, and the various `*::group()` / `*::afterParse()` methods). If a generated config is rejected on a real controller, trust the controller's `[MSG:ERR: ...]` startup messages over this document.

---

## 0. Non-negotiable file-level rules (read this section first)

These are the rules that cause silently-wrong or rejected configs when violated. An LLM generating FluidNC YAML **must** satisfy every one of these:

1. **This is not full YAML.** It is a restricted subset. Do not use YAML features not explicitly shown in this doc: no flow-style `{}`/`[]` collections, no anchors/aliases (`&`/`*` — note `&` *is* reused as a macro command separator, a completely different meaning, see §4), no multi-document `---` separators, no block scalars (`|`, `>`), no quoted-string escaping tricks beyond plain double quotes.
2. **A `#` starts a comment when it's preceded by whitespace or is the very first character of the line — never inside a quoted value — matching standard YAML.** Ground truth: `Tokenizer::findCommentStart()`/`nextLine()` (`Configuration/Tokenizer.cpp`) scan the whole line, quote-aware, *before* any key/value structure is parsed at all — so `motor0: value  # comment` and even a bare `motor0:  # comment` (a section header with a trailing note) both correctly drop everything from the `#` onward. A `#` glued directly to other text with no preceding space (e.g. a macro's G-code parameter reference like `#100`) is never treated as a comment and stays literal — there's no mechanical way to tell "meant as a comment" apart from "genuinely part of the value" from the text alone, so a value that needs a literal, whitespace-preceded `#` must be quoted (`"..."`) instead. One consequence worth knowing: a whitespace-preceded `#` appearing *before* the `:` strips the colon along with the rest of the comment; since FluidNC has no bare-scalar-document concept (unlike standard YAML), what's left is a hard parse error rather than something silently accepted. Keys can be quoted too, same as values (`"my key": value`), using the same plain, non-escaping `"..."`/`'...'` delimiter convention — not needed by any real FluidNC field name today, but supported for consistency.
3. **Indentation must be spaces only, never tabs, and consistent within a section — but the pitch is not fixed at 2.** Ground truth: the tokenizer (`Configuration/Tokenizer.cpp`) records each line's indentation as a raw leading-space count and compares it against the indent level recorded when the enclosing section was entered; it has no hardcoded "2 spaces per level" rule. Any consistent pitch (2, 3, 4 spaces, etc.) works, and different sections in the same file are even allowed to use different pitches from each other. What actually breaks parsing is *inconsistency*: all sibling keys within one section must share the exact same indent count as each other, and a nested block must be indented strictly more than its parent. Tabs are rejected outright with a parse error ("Use spaces, not tabs, for indentation"). Despite this flexibility, a generator should still pick one consistent pitch (2 spaces is the wiki/community convention) and use it everywhere, purely for human readability.
4. **Whitespace between the colon and the value is irrelevant — any amount, including zero, is accepted.** Ground truth: `Tokenizer::parseValue()` explicitly strips all leading whitespace after the colon before reading the value, so `board: 6 Pack`, `board:    6 Pack`, and `board:6 Pack` all parse identically. (Trailing whitespace on the *key* side of the colon is likewise trimmed — see `parseKey()`.) A generator should still write exactly one space after the colon as a readability convention.
5. **Trailing whitespace after a value is usually harmless, but there's one exception, so don't rely on it.** Ground truth: `Configuration/Parser.cpp` calls `string_util::trim()` on the raw token value inside almost every typed parser — `boolValue()`, `intValue()`/`uintValue()`, `floatValue()`, enum lookups, the Speed Map parser, `Pin::create()`, and `IPAddress` parsing all trim first. The one deliberate exception is `Parser::stringValue()` (plain `String` fields like `name:`, `board:`, `meta:`), which is **not** trimmed — the source comment notes *"String values might have meaningful leading and trailing spaces so we avoid trimming."* So trailing whitespace will leak into the literal value of a `String`-typed field. A generator should avoid emitting trailing whitespace anywhere.
6. **No trailing newline or blank line is required at end of file.** Ground truth: `Tokenizer::nextLine()` explicitly handles the case where the remainder has no `'\n'` left in it by treating whatever remains as the final line — *"the final line need not have a newline."*
7. **Unrecognized keys are not fatal, but they are logged, not silent.** A key FluidNC doesn't understand for that section does not throw a hard parse error — the file still loads — but it is reported at boot as `[MSG:ERR: Ignored key <name>]`. So the failure mode is an easy-to-miss log line rather than a load failure. A typo won't stop the file from loading; the machine will boot and simply not do what the misspelled key intended. Use exact, correct key names (check them against `config_items.yaml`).
8. **Keys are case-insensitive, and so are most values — the exception is plain `String`-typed values passed through to something external.** Ground truth: `Parser::isKey()` compares key names with `strncasecmp`, and enum-valued fields are matched with `string_util::equal_ignore_case()`. So `steps_per_mm`/`Steps_Per_Mm`/`STEPS_PER_MM` are equivalent, as are `stealthchop`/`StealthChop`/`STEALTHCHOP`. The exception is `String`-typed fields (`name:`, `board:`, `meta:`, …) — stored and used verbatim, so case (and exact whitespace, per §0.5) is preserved. Still match the documented spelling/casing as a matter of convention.
9. **Filename length limits are a filesystem constraint, not part of the config-file syntax.** FluidNC stores the file on the controller's onboard flash filesystem (LittleFS/SPIFFS), which imposes its own filename-length ceiling (commonly ~30 characters including `.yaml`). Worth respecting for a file that has to be uploaded and selected on real hardware, but it's a deployment concern, not a syntax rule.
10. **Do not put units in values.** Field names carry the unit as a suffix (`_mm`, `_us`, `_ms`, `_amps`, `_ohms`, `_hz`, `_mm_per_min`, `_mm_per_sec2`); the value is a bare number.
11. **Section order is free-form.** References like a motor's `uart_num: 2` are resolved inside each component's `init()`, which runs only after the entire file is parsed — so there is no forward-reference problem, and a `uartN:` section may legally appear anywhere relative to whatever references it. Placing bus/peripheral definitions near the top is still good practice for a human reader.
12. **A given physical pin must be used at most once — but the uniqueness check is scoped per pin type, not global.** Ground truth: each pin-type implementation class keeps its own static claimed-pin table (`GPIOPinDetail::_claimed`, `I2SOPinDetail::_claimed`, …) and asserts `"Pin is already used"` only against reuse within its own type. So two `gpio.16` assignments anywhere in the file collide, but `gpio.5` and `i2so.5` do not collide with each other. `NO_PIN` and `void` are both explicitly exempt from this check.
13. **Axis letter blocks can appear in any order — textual order does not matter.** Ground truth: `Axes::group()` registers each axis by name, and section matching is by key name, not file position. What *does* matter: `Axes::afterParse()` finds the highest-indexed axis present in the file and auto-creates a default `Axis` for any lower-indexed axis left undefined — so defining `z:` without `x:`/`y:` still gives you 3 axes, just with `x`/`y` silently defaulted. Minimum axis count is 3. **Define every axis you care about explicitly**; don't skip `x`/`y` while defining `z` and expect them to be absent.

---

## 1. Document structure

The config file has an implicit root (there is **no** `machine:` wrapper key — see §5). Everything is either a top-level section, a top-level scalar, or nested inside a section. `config_items.yaml` is the authoritative list; the shape is:

**Top-level sections** (each a plain key at the root):

| Kind | Keys |
|---|---|
| Singular sections | `stepping`, `i2so`, `spi`, `sdcard`, `kinematics`, `axes`, `control`, `coolant`, `probe`, `macros`, `start`, `parking`, `user_outputs`, `user_inputs`, `oled`, `status_outputs` |
| Numbered sections | `uart0`…`uart9`, `uart_channel0`…, `i2c0`… (one digit each) |
| Spindle types | one or more of `PWM`, `Laser`, `10V`, `BESC`, `HBridge`, `OnOff`, `Relay`, `DAC`, `PlasmaSpindle`, `NoSpindle`, `ModbusVFD`, plus the named-VFD keys (`Huanyang`, `H2A`, `YL620`, `DeltaMS300`, `FolinnBD600`, `H100`, `MollomG70`, `NowForever`, `SiemensV20`, `DanfossVLT2800`) — see `spindle_sections:` / `vfd_named_types:` in `config_items.yaml` |
| ATC | `atc_manual` (referenced by a spindle's `atc:` field) |
| Provisional — **do not generate** | `extenders`, `pinextender0`… |

**Top-level scalars** (bare keys at the root, siblings of the sections): `board`, `name`, `meta`, `arc_tolerance_mm`, `junction_deviation_mm`, `verbose_errors`, `report_inches`, `enable_parking_override_control`, `use_line_numbers`, `planner_blocks`. In `config_items.yaml` these are grouped under `(top-level machine items):`.

**Nesting** (from `config_items.yaml`'s dotted section paths):

```
axes:
  <group-level fields>
  x:                       # x y z a b c u v w
    <axis fields>
    homing:
      <homing fields>
    motor0:                # motor0, and optionally motor1
      <shared motor fields>
      <one driver-type block>   # standard_stepper | stepstick | tmc_2130 | tmc_2208 |
                                # tmc_2209 | tmc_5160 | tmc_2160 | tmc_5160Pro |
                                # tmc_2160Pro | rc_servo | solenoid | dynamixel2 |
                                # unipolar | null_motor
kinematics:
  <one type block>         # Cartesian | CoreXY | midtbot | ParallelDelta | WallPlotter
uart3:
  usb_host:                # optional: selects USB-host mode instead of the plain txd/rxd fields
```

`kinematics:` and every spindle type follow the same "one nested type key selects the implementation" pattern as `motorN:`.

---

## 2. Value types and how to write them

`config_items.yaml`'s `type:` field tells you which of these a value is.

| Type | How to write it |
|---|---|
| **Boolean** | `true` or `false`, unquoted (case-insensitive). **Gotcha:** `Parser::boolValue()` only checks whether the token equals `"true"`; anything else — `flase` (typo), `nope`, `0` — silently becomes `false` with **no error**. Write exactly `true` or exactly `false`. |
| **Integer** | A bare **decimal** integer, no decimal point. `16` is valid; `16.0` is a Float and will likely error for an Integer field. **Hex literals (`0x...`) are rejected** — `string_util::from_decimal()` requires all-decimal digits, so `0x3c` produces a hard `"Expected an integer value"`. Write I2C addresses etc. in decimal (`60`, never `0x3c`). |
| **Float** | Decimal number, e.g. `800.000`. Some fields are positive-only — see the field's range in `config_items.yaml`. |
| **String** | Plain text, generally max 255 chars. Quote with `"..."` when the string contains a colon, leading/trailing space, or looks like another YAML type; otherwise quoting is optional. |
| **Pin** | See §3. |
| **Enumeration** (`type: enum`) | One of the token list in the item's `values:`. Case-insensitive at parse time; use the documented spelling (e.g. `StealthChop`, not `STEALTHCHOP`). |
| **UartData** (`type: uart_mode`) | A 3-char mode string: data bits (5–8), parity (`N`/`E`/`O`), stop bits (1–2), e.g. `8N1`, `8E1`, `7O1`. **Always quote it** (`mode: "8E1"`): `8E1` is also a valid scientific-notation float (80), and generic-YAML tooling in the ecosystem can silently read an unquoted `8E1` as the number `80`. |
| **Speed Map** (`type: std::vector<Configuration::speedEntry>`) | See below. |
| **Float Array** (`type: std::vector<float>`) | See below. |
| **Macro** (`type: macro`) | A single line — see §4's macro rules. |

### Speed Map grammar

A piecewise-linear mapping from GCode S-word values to a percentage of the spindle's device-specific maximum output (PWM duty, DAC level, VFD RPM range, …). FluidNC linearly interpolates between consecutive breakpoints at runtime. Ground truth: `Parser::speedEntryValue()`.

```
speed_map := entry (" "+ entry)*
entry      := s_value "=" percent "%"
s_value    := non-negative decimal integer        (no decimals, no negatives)
percent    := float                               (decimals allowed, e.g. 100.000)
```

- Entries are whitespace-separated (any run of spaces). Each entry is split on its **first** `=` and **first** `%`.
- The trailing `%` is not actually enforced by the parser, but **always write it** — it's the conventional form throughout FluidNC.
- S-values should be monotonically increasing; the first entry is typically `0=0.000%` and the last the maximum expected `S` at `100.000%`. Non-monotonic entries produce nonsensical interpolation.
- If the whole value is empty or fails to parse, FluidNC logs `"Using default speed map"` and falls back to a built-in default — it does **not** error the file load.

Example: `speed_map: "0=0.000% 1000=0.000% 24000=100.000%"` — space-separated `S=percent%` pairs, no commas.

### Float Array grammar

Ground truth: `Parser::floatArray()`.

```
float_array := float (" "+ float)*
```

Plain whitespace-separated floats — **no brackets, no commas, no other delimiter**. `[0.0, 0.0, ...]` is *not* valid. Provide one value per defined axis, in axis-letter order (that's how `change_mpos_mm`/`ets_mpos_mm` are consumed), though the parser doesn't enforce a count. If the value is empty or a float fails to parse, the whole array is silently discarded (`"Using default value"`) — a wrong-syntax array won't fail the load, it just behaves as if never set.

---

## 3. Pin grammar

Ground truth: `Pin::parse()` in `Pin.cpp` is the single dispatch point for every pin-type prefix. The valid prefixes and their `.` / attribute shapes are also machine-listed in `config_items.yaml`'s `pin_namespaces:` block.

```
pin           := pin_type [ "." pin_number ] [ ":" attribute ]*
pin_type      := "gpio" | "i2so" | "uart_channel" digit | "no_pin" | "void" | "pinext" digit
pin_number    := non-negative integer   (no board-legality check performed by this spec)
attribute     := ("high" | "low")       (active state; default high)
               | ("pu" | "pd")          (pull-up / pull-down; input pins only; default floating)
               | ("ds" digit)           (drive strength, digit 0-3; gpio output pins only; default 2)
special_value := "NO_PIN"               (canonical "no pin", stands alone with no dot/number)
```

- Attributes are colon-prefixed, chain in **any order**: `gpio.16:low:pu`.
- Pin-type matching is case-insensitive (`GPIO.16` == `gpio.16`); prefer lowercase.
- `NO_PIN` is the universal "no pin assigned" sentinel and the default for essentially every pin field. Written bare — not `gpio.NO_PIN`.

| Prefix | Meaning |
|---|---|
| `gpio.N` | Native MCU pin (ESP32 or, on a dev build, RP2040 — same grammar). |
| `i2so.N` | Output-only pin on an external I2S shift-register chain. Requires an `i2so:` section (§4) and an I2S stepping engine. Cannot do PWM. |
| `uart_channelN.M` | Advanced/rare: a virtual pin carried over a UART "channel" — `N` selects the `uart_channelN:` section, `M` is the virtual pin index. Supports `:pu`/`:pd`/`:low`/`:high`, not `:ds`. |
| `void` | A real allocatable pin that does nothing (accepts writes, reports reads, drives no hardware). Written bare (`void`, not `void.0`). Exempt from the duplicate-pin check (§0.12) — useful for bench-testing or disabling one part of a machine. |
| `NO_PIN` | "Nothing connected here." |
| `pinextN.M` | **DO NOT USE.** Pin on an I2C GPIO-expander configured under the provisional `extenders:` feature (which may be removed). If you need spare I/O, use `user_outputs:`/`user_inputs:` or native `gpio.N`. Trap for existing configs: the pin prefix is `pinext<n>` but the section key is `pinextender<n>:` — two different literal strings. |

---

## 4. Whole-document and cross-field rules

`config_items.yaml` documents each field in isolation. These rules span multiple fields or whole sections and are **not** expressible there — an LLM must apply them itself.

### Stepping and pins

- **`i2so:` section is required if any `i2so.N` pin appears anywhere in the file.** Without it those pins do not function.
- **Do not mix `i2so.N` pins with `stepping.engine: Timed` or `RMT`.** I2SO pins require an I2S stepping engine (`I2S_STATIC` / `I2S_STREAM`). `I2S_STATIC` and `I2S_STREAM` are functionally identical (historical names). `RMT` is compiled in only on boards with `MAX_N_RMT`; the I2S engines only with `MAX_N_I2SO` — an `engine:` value valid on one board build may not exist on another.
- **`stepping.idle_ms` default `255` is a magic value**, not an ordinary millisecond count — it means "never auto-disable motors," and it is also the out-of-the-box default. So an unconfigured `stepping:` already leaves motors permanently enabled; any value 0–254 or 256+ is a real delay before auto-disable.
- **`pulse_us` / `dir_delay_us` cap the achievable step rate.** At load time FluidNC checks `1000000 / (2*pulse_us + dir_delay_us)` against `steps_per_mm * max_rate_mm_per_min / 60` and throws `"Stepping rate N steps/sec exceeds the maximum rate M"` if it can't keep up. Keep `steps_per_mm` no higher than needed (lower microstepping if margin is tight).

### Axes and homing

- **Axis auto-fill / minimum 3** — see §0.13. Also: `x`/`y`/`z` are linear axes (scaled for inches under G20); `a`/`b`/`c` (and `u`/`v`/`w`) are rotary/universal-unit and never inch-scaled, even if used for a physical linear motion.
- **`soft_limits: true` is only safe after homing.** `max_travel_mm` is measured from the pulled-off homing-switch position, not an arbitrary datum — account for `pulloff_mm`, don't just copy a machine's physical travel spec.
- **`homing.cycle` is not a boolean.** `-1` = axis does not move during homing, its `mpos_mm` is just assigned (for axes with no switch). `0` (**the default**) = excluded from group `$H` but still individually homeable with `$H<axis>` — so an axis left entirely at `homing:` defaults is *not* in `$H`; `cycle` must be `1` or higher for that. `1`+ = homes as part of `$H`; axes sharing a cycle number home simultaneously (convention: Z alone on `cycle: 1`, then X/Y together on `cycle: 2`).
- **Multi-axis homing (same cycle number for 2+ axes) cannot be used with CoreXY** kinematics — CoreXY drives two motors per logical axis move.
- **A motor using `limit_all_pin` must have that switch manually cleared before homing** — the firmware can't tell which way to back off.

### Motors and drivers

- **Exactly one driver-type block per `motorN:`.** Shared motor-level keys (`limit_neg_pin`, `limit_pos_pin`, `limit_all_pin`, `hard_limits`, `pulloff_mm`) sit alongside it.
- **`limit_all_pin` is mutually exclusive with `limit_neg_pin` / `limit_pos_pin` on the same motor.** Use `limit_all_pin` only when both ends share one physical switch/circuit.
- **Use `stepstick:`, not `tmc_2209:` / `tmc_2208:`, for a Trinamic chip whose UART is not actually wired up** (an onboard chip with no UART broken out, or a stepstick module jumpered for standalone mode). The `tmc_*` UART sections only do something if there's a working UART link to write registers over; otherwise set current via the module's trimpot and microstepping via `ms1_pin`/`ms2_pin`/`ms3_pin`.
- **`tmc_2160` is an alias of `tmc_5160`** (ordinary semantic-field SPI driver). Despite the name it is **not** related to `tmc_2160Pro`, which — like `tmc_5160Pro` — is a raw-register expert-mode driver whose fields are 32-bit register values, not semantic settings. Do not invent raw register values.
- **TMC daisy chain (SPI, `tmc_2130`/`tmc_5160`):** FluidNC must know about every driver position in the physical chain. Define a motor entry for each position, using placeholder values for unused positions, or the chain's data alignment breaks.
- **TMC2208 daisy chain:** TMC2208 is not individually addressable over UART (its `addr:` field has no hardware address pins behind it). If multiple TMC2208 motors share one UART, the register values actually applied are **whichever motor is defined LAST in the file**; earlier per-motor current/microstep values are silently overridden. Don't expect distinct per-motor settings.
- **TMC2209 shared address:** multiple selectorless TMC2209s may share one `uart_num:`+`addr:` only when every member sets `shared_address_write_only: true`, uses `cs_pin: NO_PIN`, and has identical UART-controlled settings. In that mode register readback and live StallGuard are unavailable and `stallguard_debug: true` is rejected.
- **`tmc_*` `uart_num:` must reference a top-level `uartN:` section.** There is no per-motor nested `uart:` sub-block.
- **`unipolar:` has no `step_pin`/`direction_pin`.** The four `phaseN_pin`s are the whole pin set and must be `gpio.` pins (rejected otherwise). Reverse direction by swapping `phase0`↔`phase3` and `phase1`↔`phase2`. Setting `half_step: false` halves steps/rev, so halve the axis's `steps_per_mm` to match.
- **`rc_servo:` / `solenoid:` / `dynamixel2:`:** reverse direction by swapping the two end values (`min_pulse_us`/`max_pulse_us`, or `count_min`/`count_max`), not by inverting a pin. `soft_limits: true` is strongly recommended on a servo axis. `dynamixel2` `id:` default `255` is the broadcast address — always set a real per-servo ID.

### Buses

- **`spi:` and `sdcard:` are required together** — defining one without the other leaves the SD card non-functional.
- **`uart_num: N` references a top-level `uartN:` section** (order-free, §0.11). Baud/mode for a Trinamic driver or a VFD live on that `uartN:` section, not on the driver/spindle.
- **`usb_host:` is a nested subsection of a `uartN:` block** selecting a USB virtual COM port instead of hardware UART pins (ESP32-S3). When present, the plain `txd_pin`/`rxd_pin`/`baud`/`mode` fields of that `uartN:` do not apply.
- **A `uart_channelN.M` pin** needs a `uart_channelN:` section, which in turn needs `uart_num:` pointing at a real `uartN:`.

### Spindles

- **There is no spindle wrapper key** — see §5. A spindle type name is a direct top-level key.
- **The spindle class hierarchy explains field-set differences** (see each type's section-level note in `config_items.yaml`): `DAC` derives from `OnOff`, not `PWM`, so it has **no `pwm_hz`**, and its `output_pin` must be `gpio.25` or `gpio.26` (ESP32 DAC pins). `Laser` and `HBridge` have **no `direction_pin`**. `PlasmaSpindle` has **no `output_pin`** and **no `spinup_ms`/`spindown_ms`**. `Relay` and `OnOff` are the same class under two names. `Laser` configs should set `off_on_alarm: true` (unlike the `false` default elsewhere).
- **Multi-spindle tool ranges:** each spindle's `tool_num:` marks the **start** of its tool range, up to the next spindle's `tool_num:`. Exactly one spindle must have `tool_num: 0`. `MachineConfig::afterParse()` forces the first spindle to `0` (with a warning) if none was, and bumps a duplicate/out-of-order tool number by 100 (also with a warning). Two spindles of the same type both work, but `$`-command introspection only ever reports the first.
- **VFD command strings** (`ModbusVFD` `cw_cmd`/`ccw_cmd`/`set_rpm_cmd`/…) are opaque protocol tokens — take them from the specific VFD's Modbus register documentation, never invent them. `safety_polling` is `ModbusVFD`-only (the named-protocol VFD types hardcode it per model).
- **`NoSpindle:` is auto-created** if no spindle section exists at all; write it explicitly only for clarity.

### Macros

- **A macro's entire body must fit on one physical line** of the file — a hard constraint.
- Multiple commands in one macro line are separated by `&`. This is a **config-macro-only** separator — not valid inside a `.nc` file run via `$SD/Run`, and not general GCode.
- If a sequence is too long for one line, put it in a separate `.nc` file (one command per line, no `&`) and make the macro body `$SD/Run=filename`.
- Realtime single-character commands are embedded as `#` followed by a documented 2-letter code (e.g. `#fr` for feed-override reset), not the raw byte.
- An empty macro key (`macro2:` with nothing after it) is valid and means "do nothing."

### Control, probe, parking, kinematics

- **Every `control:` input must read inactive at boot** or FluidNC raises an "active at startup" alarm — fix by flipping the pin's `:high`/`:low` attribute (§3), not by changing the section structure. `estop_pin` and `fault_pin` are wired to the *same* internal event — they are not two independent behaviors.
- **The `probe:` feature counts as present only if `pin` or `toolsetter_pin` is defined.** An all-default `probe:` block is equivalent to omitting the section.
- **`kinematics:` may be omitted** for a plain 3-axis Cartesian machine — `Cartesian` is auto-created. `CoreXY.x_scaler` stays `1.0` unless the motors themselves move in X (midTbot-style).
- **`enable_parking_override_control`** (a top-level scalar) additionally allows GCode `M56` to toggle parking at runtime; **`parking.enable`** is the base switch for the feature existing at all.

### A couple of stale example-file traps

- `example_configs/uartio.yaml` contains `all_messages:` under a `uart_channelN:` — **not a valid key**, the example is stale.
- Older/generator-only VFD configs use a nested `uart:` sub-block under the spindle instead of `uart_num:` — use `uart_num:` + a top-level `uartN:` section.

---

## 5. Common LLM-generation mistakes

> **⚠️ The spindle wrapper.** Three different AI assistants (Claude, GitHub Copilot, and Gemini), independently and in separate incidents, have all generated a wrapper key around spindle content that **does not exist** — variously `spindle:`, `Spindle:`, and `spindles:` (singular and plural), sometimes with a nested `type:` field, sometimes with the type name as a nested key under the invented wrapper. All of these are wrong, in any casing. **There is no wrapper of any kind.** It's a convergent mistake because a wrapper is how nearly every other config format would design this — knowing *why* it's tempting doesn't make it correct. **A spindle type name (`PWM`, `Relay`, `ModbusVFD`, …) is placed directly at the document root, as its own top-level key, exactly like `axes:` or `control:`.** The same applies to the document as a whole: there is no `machine:` root wrapper either.

Other recurring mistakes:

- Treating `homing.cycle` as a boolean (see §4 — `0` means "not in `$H`").
- Bracket/comma syntax for a Float Array or Speed Map (`[0.0, 0.0]`) — both are plain space-separated (§2).
- Hex integers (`0x3c`) — rejected; write decimal (§2).
- Unquoted `mode: 8E1` — quote all UartData values (§2).
- Adding `pwm_hz` to a `DAC:` spindle, or `direction_pin` to `Laser:`/`HBridge:` — those fields don't exist on those types (§4, `config_items.yaml`).
- Emitting `extenders:` / `pinextN.M` pins — provisional, do not use; prefer `user_outputs:`/`user_inputs:` (§3).
- Assuming a `homing:` block left at defaults puts the axis in `$H` (it doesn't).
- Putting units in a value (`max_rate_mm_per_min: 4500 mm/min`) — bare number only (§0.10).

**Always validate** what you produce — via the MCP server, `tools/validate_fluidnc_config.py`, or by checking against `config_items.yaml` + this document. The validator works on a bare fragment (e.g. just a `Relay:` block); you don't need a complete file.

---

## 6. Worked examples

### 6.1 Minimal 3-axis RMT-stepping machine, external stepstick drivers, no SD card, one PWM spindle

```yaml
name: "Minimal 3 Axis Router"
board: "Generic"

stepping:
  engine: RMT
  idle_ms: 255
  pulse_us: 2
  dir_delay_us: 1
  disable_delay_us: 0

axes:
  shared_stepper_disable_pin: gpio.13:low

  x:
    steps_per_mm: 80.000
    max_rate_mm_per_min: 4500.000
    acceleration_mm_per_sec2: 100.000
    max_travel_mm: 300.000
    soft_limits: true
    homing:
      cycle: 2
      positive_direction: false
      mpos_mm: 0.000
    motor0:
      limit_all_pin: gpio.4:low:pu
      hard_limits: true
      pulloff_mm: 2.000
      stepstick:
        step_pin: gpio.12
        direction_pin: gpio.14
    motor1:
      null_motor:

  y:
    steps_per_mm: 80.000
    max_rate_mm_per_min: 4500.000
    acceleration_mm_per_sec2: 100.000
    max_travel_mm: 300.000
    soft_limits: true
    homing:
      cycle: 2
      positive_direction: false
      mpos_mm: 0.000
    motor0:
      limit_all_pin: gpio.16:low:pu
      hard_limits: true
      pulloff_mm: 2.000
      stepstick:
        step_pin: gpio.27
        direction_pin: gpio.26
    motor1:
      null_motor:

  z:
    steps_per_mm: 400.000
    max_rate_mm_per_min: 1500.000
    acceleration_mm_per_sec2: 50.000
    max_travel_mm: 80.000
    soft_limits: true
    homing:
      cycle: 1
      positive_direction: true
      mpos_mm: 0.000
    motor0:
      limit_all_pin: gpio.17:low:pu
      hard_limits: true
      pulloff_mm: 2.000
      stepstick:
        step_pin: gpio.33
        direction_pin: gpio.32
    motor1:
      null_motor:

control:
  estop_pin: gpio.34

coolant:
  flood_pin: NO_PIN
  mist_pin: NO_PIN

probe:
  pin: gpio.35:low:pu

macros:
  after_homing: g0 x0 y0

PWM:
  pwm_hz: 5000
  output_pin: gpio.2
  enable_pin: NO_PIN
  direction_pin: NO_PIN
  disable_with_s0: false
  s0_with_disable: true
  spinup_ms: 500
  spindown_ms: 500
  tool_num: 0
  speed_map: "0=0.000% 1000=100.000%"
  off_on_alarm: true
```

### 6.2 TMC2209 UART daisy fragment (illustrates §4's `uart_num` + shared-bus rules)

```yaml
uart1:
  txd_pin: gpio.22
  rxd_pin: gpio.21
  baud: 115200
  mode: "8N1"

axes:
  x:
    steps_per_mm: 160.000
    max_rate_mm_per_min: 6000.000
    acceleration_mm_per_sec2: 200.000
    max_travel_mm: 400.000
    motor0:
      limit_neg_pin: gpio.36:low
      hard_limits: true
      pulloff_mm: 2.000
      tmc_2209:
        uart_num: 1
        addr: 0
        step_pin: gpio.12
        direction_pin: gpio.14
        run_amps: 1.000
        hold_amps: 0.500
        microsteps: 16
        run_mode: StealthChop
        homing_mode: StealthChop
        use_enable: true

  y:
    steps_per_mm: 160.000
    max_rate_mm_per_min: 6000.000
    acceleration_mm_per_sec2: 200.000
    max_travel_mm: 400.000
    motor0:
      limit_neg_pin: gpio.39:low
      hard_limits: true
      pulloff_mm: 2.000
      tmc_2209:
        uart_num: 1
        addr: 1
        step_pin: gpio.27
        direction_pin: gpio.26
        run_amps: 1.000
        hold_amps: 0.500
        microsteps: 16
        run_mode: StealthChop
        homing_mode: StealthChop
        use_enable: true
```

`uart1:` is placed before `axes:` for readability (not required, §0.11); each motor uses a distinct `addr:` on the shared `uart_num: 1` bus.

---

## 7. Scope and limits

- **Board-specific pin legality** (which GPIO numbers exist/are usable on a given board) is intentionally out of scope — pin identifiers are validated only for grammar (§3).
- **`config_items.yaml` is the field reference.** If it and this document disagree, it wins; it is regenerated from source on every change and cannot drift.
- Cross-field rules that this document lists in §4 are not enforced by the validator schema (the validator checks structure, type, range, and enum only). Apply them yourself.
