# FluidNC `config.yaml` Formal Specification (for LLM Generation)

**Purpose:** This document gives an LLM everything it needs to generate a syntactically and structurally correct FluidNC configuration file, and to avoid the specific mistakes that are common when generating YAML from a language model. It intentionally does **not** validate board-specific pin legality (e.g. "is GPIO 6 usable on this particular board") — pin identifiers are treated as an opaque grammar (see §3), not a board-specific lookup table. That is a planned later elaboration into a JSON Schema. Note also that FluidNC's primary target is the ESP32, but a development build also runs on RP2040 — nothing in this document assumes ESP32-specific hardware unless explicitly noted (the one confirmed exception is DAC-type spindle output, §10.5, which relies on an ESP32-specific peripheral).

**Sources, and a note on methodology:** §§0–12 were originally sourced from wiki.fluidnc.com (config/overview, config_IO, axes, control, coolant, probe, macros, config_spindles, sd_card) and real-world example configs. §§13+ (and the spindle corrections folded into §10) were instead derived directly from the **FluidNC source code** (github.com/bdring/FluidNC, `main` branch, cloned mid-2026), which is the actual ground truth: every config section corresponds to a `handler.section(name, ...)` call, and every field within it to a `handler.item(name, ...)` call, both found by grepping for `::group(Configuration::HandlerBase&`. The C++ type of the target variable in each `item()` call determines the value grammar (see the `ParserHandler::item()` overload table in `Configuration/ParserHandler.h` — `int32_t`/`uint32_t` w/ min-max → Integer, `float` w/ min-max → Float, `bool` → Boolean, `Pin`/`InputPin`/`EventPin` → Pin, `std::string` → String, `Macro` → macro line, `std::vector<speedEntry>` → Speed Map, `UartData`+`UartParity`+`UartStop` → UartData). Default values come from the in-class member initializers in the corresponding `.h` file. This is a strictly more reliable method than reading wiki prose or scraping example configs, and supersedes the wiki wherever the two disagree — treat §§13+ and the corrected parts of §10 as higher-confidence than the rest of the document.

FluidNC evolves; if a generated config is rejected on a real controller, trust the controller's `[MSG:ERR: ...]` startup messages over this document, and re-derive from source if a discrepancy is found.

---

## 0. Non-negotiable file-level rules (read this section first)

These are the rules that cause silently-wrong or rejected configs when violated. An LLM generating FluidNC YAML **must** satisfy every one of these:

1. **This is not full YAML.** It is a restricted subset. Do not use YAML features not explicitly shown in this doc: no flow-style `{}`/`[]` collections, no anchors/aliases (`&`/`*` — note `&` *is* reused as a macro command separator, a completely different meaning, see §11), no multi-document `---` separators, no block scalars (`|`, `>`), no quoted-string escaping tricks beyond plain double quotes.
2. **No comments after a key: value pair on the same line.** `motor0:  # comment` is a parse error. A comment must be alone on its own line, starting with `#` at the start of the line (indentation before the `#` is fine).
3. **Indentation must be spaces only, never tabs, and consistent within a section — but the pitch is not fixed at 2.** Ground truth: the tokenizer (`Configuration/Tokenizer.cpp`) records each line's indentation as a raw leading-space count and compares it against the indent level recorded when the enclosing section was entered; it has no hardcoded "2 spaces per level" rule. Any consistent pitch (2, 3, 4 spaces, etc.) works, and different sections in the same file are even allowed to use different pitches from each other. What actually breaks parsing is *inconsistency*: all sibling keys within one section must share the exact same indent count as each other, and a nested block must be indented strictly more than its parent. Tabs are rejected outright with a parse error ("Use spaces, not tabs, for indentation"). Despite this flexibility, a generator should still pick one consistent pitch (2 spaces is the wiki/community convention) and use it everywhere, purely for human readability — not because the parser requires it.
4. **Whitespace between the colon and the value is irrelevant — any amount, including zero, is accepted.** Ground truth: `Tokenizer::parseValue()` (`Configuration/Tokenizer.cpp`) explicitly strips all leading whitespace after the colon before reading the value, so `board: 6 Pack`, `board:    6 Pack`, and `board:6 Pack` all parse identically. (Trailing whitespace on the *key* side of the colon is likewise trimmed — see `parseKey()`.) A generator should still write exactly one space after the colon purely as a readability convention, not because the parser requires it.
5. **Trailing whitespace after a value is usually harmless, but there's one exception, so don't rely on it.** Ground truth: `Configuration/Parser.cpp` calls `string_util::trim()` on the raw token value inside almost every typed parser — `boolValue()`, `intValue()`/`uintValue()`, `floatValue()`, enum lookups, the Speed Map parser, `Pin::create()`, and `IPAddress` parsing all trim first, so trailing (and leading) spaces are silently discarded for those types. The one deliberate exception is `Parser::stringValue()` (plain `String` fields like `name:`, `board:`, `meta:`), which is **not** trimmed — the source comment notes *"String values might have meaningful leading and trailing spaces so we avoid trimming."* So trailing whitespace is generally fine, except it will leak into the literal value of a `String`-typed field. A generator should still avoid emitting trailing whitespace anywhere, both for readability and to sidestep that one exception entirely.
6. **No trailing newline or blank line is required at end of file.** Ground truth: `Tokenizer::nextLine()` (`Configuration/Tokenizer.cpp`) explicitly handles the case where `_remainder` has no `'\n'` left in it by treating whatever remains as the final line — the source comment states outright *"the final line need not have a newline."* A file whose very last byte is the final character of the last value parses identically to one with a trailing newline. A generator may still end the file with a newline as an ordinary text-file convention, but it is not a parser requirement and no blank line after it is needed either.
7. **Unrecognized keys are not fatal, but they are logged, not silent.** A key FluidNC doesn't understand for that section does not throw a hard parse error — the file still loads — but it is reported at boot as `[MSG:ERR: Ignored key <name>]`. So the failure mode is not silence; it's an easy-to-miss log line rather than a load failure. Because of this, an LLM must still be extra careful to use exact, correct key names — a typo won't stop the file from loading, so the machine will boot and simply not do what the misspelled key intended, with only that boot-log line as a clue.
8. **Keys are case-insensitive, and so are most values — the exception is plain `String`-typed values that get passed through to something external.** Ground truth: `Parser::isKey()` compares key names with `strncasecmp` (case-insensitive), and enum-valued fields (e.g. `run_mode:`, `engine:`) are matched with `string_util::equal_ignore_case()`. So `steps_per_mm`, `Steps_Per_Mm`, and `STEPS_PER_MM` are all equivalent, as are `stealthchop`/`StealthChop`/`STEALTHCHOP` for an enum field. The exception is `String`-typed fields (`name:`, `board:`, `meta:`, and similar free-text values) — these are stored and used verbatim, so their case (and exact whitespace, per §0.5) is preserved and can matter if that string is later compared against something case-sensitive elsewhere (e.g. a value handed to external software). Note that secrets like WiFi/AP passwords are not part of `config.yaml` at all — they live in NVS settings (`$` commands), not this file — so case-sensitivity of credentials isn't a concern here. A generator should still match the documented spelling/casing shown in this doc as a matter of convention and readability, even though the parser itself won't reject a differently-cased key or enum.
9. **Filename length limits are a filesystem constraint, not part of the config-file syntax itself.** If you are naming the output file, keep in mind FluidNC stores it on the controller's onboard flash filesystem (LittleFS/SPIFFS), which imposes its own filename-length ceiling (commonly cited as ~30 characters including `.yaml`) — this is a property of the filesystem FluidNC happens to run on, not a rule the config-file grammar or parser enforces. Still worth respecting for a file that has to actually be uploaded and selected on real hardware, but it's a deployment concern rather than a syntax rule, and it's worth re-verifying against the specific filesystem/board in use rather than treating 30 as a universal constant.
10. **Do not put units in values.** Field names carry the unit as a suffix (`_mm`, `_us`, `_ms`, `_amps`, `_ohms`, `_hz`, `_mm_per_min`, `_mm_per_sec2`); the value itself is a bare number.
11. **Section order is free-form; putting bus-definition sections (`uartN:`, `i2cN:`, `spi:`) early is a readability convention, not a parser requirement.** Ground truth: references like a motor's `uart_num: 2` are resolved via lookups such as `config->_uarts[_uart_num]` inside each component's `init()`, which only runs after the entire file has been parsed and every top-level array (`_uarts[]`, etc.) is fully populated — so there is no actual forward-reference problem, and a `uartN:` section may legally appear anywhere in the file relative to whatever references it. Placing bus/peripheral definitions near the top is still good practice for a human (or LLM) reading the file, since it lets the reader resolve `uart_num: 2` mentally before encountering it, but it changes nothing about how the file loads.
12. **A given physical pin must be used at most once — but the uniqueness check is scoped per pin type, not global across the whole file.** Ground truth: each pin-type implementation class keeps its own static claimed-pin table and asserts on reuse — e.g. `Pins::GPIOPinDetail::_claimed` (`Pins/GPIOPinDetail.cpp`) and `Pins::I2SOPinDetail::_claimed` (`Pins/I2SOPinDetail.cpp`) are two *separate* tables, each raising `"Pin is already used"` only against reuse within its own type. Practically: two different `gpio.16` assignments anywhere in the file will correctly collide, but `gpio.5` and `i2so.5` don't collide with each other despite sharing the number 5, because they're tracked independently. `NO_PIN` and `void` (§3) are both explicitly exempt from this check — `NO_PIN` because it means "nothing here," and `void` because each `void` pin instance is freshly allocated with no shared state at all (the source comment notes multiple `void` pins are intentionally useful for debugging).
13. **Axis letter blocks (`x:`, `y:`, `z:`, `a:`, `b:`, `c:`) can appear in any order in the file — textual order does not matter.** Ground truth: `Axes::group()` registers each axis via `handler.section(_axisNames[axis], _axis[axis], axis)`, and section matching throughout the parser is done by key **name**, not by sequential file position (the same way `uartN:`/`kinematics:`/etc. can appear anywhere, per §0.11). What *does* matter is which axis **indices** end up defined at all: `Axes::afterParse()` finds the highest-indexed axis actually present in the file and auto-creates a default `Axis` object for any lower-indexed axis that was left undefined — so if you define `z:` but never mention `x:` or `y:`, you still end up with 3 axes, just with `x`/`y` silently defaulted rather than configured the way you intended. The minimum axis count is 3 (`_numberAxis` is forced up to `A_AXIS` if fewer are found). Practical takeaway for a generator: define every axis you actually care about explicitly, in whatever order is convenient — but don't skip `x`/`y` while defining `z` and expect them to be absent; they'll exist with defaults regardless.

---

## 1. Top-level document skeleton

Ground truth: `Machine::MachineConfig::group()` in `Machine/MachineConfig.cpp`. This is the complete, exact list of everything a top-level `machine:` (implicit root) node accepts — nothing else exists at this level.

```yaml
board: "<string>"          # handler.item, String
name: "<string>"           # handler.item, String
meta: "<string>"           # handler.item, String — free-form notes, e.g. a build date/description

stepping:                  # handler.section — singular, see §4
  # ...

uart1:                     # handler.sections("uart", 1, MAX_N_UARTS, ...) — numbered uart1..uartN, see §9
uart_channel1:              # handler.sections("uart_channel", 1, MAX_N_UARTS, ...) — see §9.1

i2so:                       # handler.section, singular (only one I2S bus supported), see §3.6
i2c1:                        # handler.sections("i2c", 0, MAX_N_I2C, ...) — numbered i2c0..i2cN, see §18

spi:                         # handler.section, singular, see §6
sdcard:                      # handler.section, singular, see §6

kinematics:                  # handler.section — see §13
axes:                        # handler.section — see §5

control:                     # handler.section — see §7
coolant:                     # handler.section — see §8
probe:                       # handler.section — see §8.1
macros:                      # handler.section — see §11
extenders:                    # handler.section — see §19 (DO NOT USE — provisional)
start:                        # handler.section — see §16
parking:                      # handler.section — see §16

user_outputs:                  # handler.section — see §15
user_inputs:                   # handler.section — see §15

pinextender0:                   # DO NOT USE — provisional, may be removed. See §19.
oled:                            # via ConfigurableModuleFactory::factory — see §21 (optional status display)
rgbled:                           # DO NOT USE — Listeners/SysListener framework likely to be removed. See §22.
atc_manual:                        # via ATCs::ATCFactory::factory — see §17 (referenced by a spindle's atc: field)
<SpindleTypeName>:               # via Spindles::SpindleFactory::factory — one or more, see §10/§14

# Top-level scalar items, siblings of the above, not nested under any section:
arc_tolerance_mm: 0.002                   # Float, 0.001-1.0, default 0.002
junction_deviation_mm: 0.01                # Float, 0.01-1.0, default 0.01
verbose_errors: true                        # Boolean, default true
report_inches: false                         # Boolean, default false
enable_parking_override_control: false        # Boolean, default false — gates M56 support
use_line_numbers: false                        # Boolean, default false
planner_blocks: 16                              # Integer, 10-120, default 16
```

There is no single fixed required ordering of these top-level blocks (except the forward-reference rule in §0.11), and none of them are individually mandatory *except* that some minimal, valid `axes:` definition (x, y, z at minimum) is expected for any real machine. A totally empty/absent section (e.g. no `probe:` section at all) simply means that feature is disabled with defaults — confirmed by `MachineConfig::afterParse()`, which explicitly constructs a default instance of `_axes`, `_coolant`, `_kinematics`, `_probe`, `_userOutputs`, `_userInputs`, `_control`, `_start`, `_parking`, and (if `MAX_N_SDCARD`/`MAX_N_SPI`) `_sdCard`/`_spi` whenever the corresponding pointer is still null after parsing.

`name:`, `board:`, and `meta:` are free-form descriptive strings — informational only, not validated against a board list.

---

## 2. Data types

| Type | Grammar / rule |
|---|---|
| **Boolean** | Write `true` or `false`, unquoted (case doesn't matter, per §0.8). **Important gotcha:** ground truth (`Parser::boolValue()` in `Configuration/Parser.cpp`) shows this is not a validated two-value type at all — the parser only checks whether the (trimmed, case-insensitive) token equals `"true"`; if it doesn't, the field is simply set to `false`, with **no error**, no matter what the actual token was. This means `enabled: flase` (typo), `enabled: nope`, or `enabled: 0` all silently produce `false` exactly as if you'd written `enabled: false` correctly — there is no parser safety net catching a misspelled boolean. Always write the value as exactly `true` or exactly `false`; never rely on any other token to mean what you think it means. |
| **Float** | Decimal number, up to 3 decimal places, e.g. `800.000`. Some fields are constrained to positive values only — see each field's documented range. |
| **Integer** | A bare, plain **decimal** integer with **no decimal point**. `16` is valid; `16.0` will be parsed as a Float and is likely to error for an Integer field. **Hex literals (`0x...`) are rejected, not just discouraged** — ground truth: `string_util::from_decimal()` (`string_util.cpp`) requires every character to be a plain decimal digit (or, for the signed path, uses `std::from_chars` with an implicit base of 10), so `0x3c` fails to parse as decimal, then fails again as a float, producing a hard `"Expected an integer value"` error. Always write integers in plain decimal, even for values conventionally shown in hex elsewhere (e.g. I2C addresses) — write `60`, never `0x3c`. |
| **String** | Plain text, generally max length 255 characters (specific fields may be shorter, e.g. filenames ≤ 30 chars). Quote with `"..."` when the string contains a colon, leading/trailing space, or looks like another YAML type; otherwise quoting is optional. |
| **Pin** | See §3. |
| **UartData** | A 3-character mode string: 1 digit data bits (5–8), 1 parity letter (`N`/`E`/`O`), 1 digit stop bits (1–2), e.g. `8N1` (standard), `8E1`, `7O1`. **Always quote this value** (`mode: "8E1"`), even though FluidNC's own parser doesn't require it. The reason is a known real-world failure: `8E1` is also a syntactically valid floating-point number in scientific notation (8 × 10¹ = 80), and other tools in the ecosystem that read FluidNC configs as generic YAML (editors, linters, config-generation/validation tooling built against standard YAML rather than FluidNC's own subset) can misinterpret an unquoted `8E1` as the number `80` rather than the string `"8E1"`, silently corrupting the value before it ever reaches FluidNC. `8N1`/`8O1` aren't ambiguous this way, but quoting all UartData values uniformly is the simplest way to avoid this class of bug entirely. |
| **Enumeration** | One of a fixed, case-sensitive list of string tokens documented per field (e.g. `run_mode:` is one of `StealthChop`, `CoolStep`, `Stallguard`). |
| **Speed Map** | A specially formatted string, see §10.1. |
| **Float Array** | Whitespace-separated floats, no brackets, no commas (e.g. `0.0 0.0 0.0 0.0 0.0 0.0`) — see §17 for the full grammar and its silent-failure behavior. Used by `atc_manual:`'s `change_mpos_mm`/`ets_mpos_mm`. |

---

## 3. Pin grammar (opaque as to board legality, but syntax IS specified)

Ground truth: `Pin::parse()` in `Pin.cpp` is the single dispatch point for every pin-type prefix that exists. There are more types than just `gpio`/`i2so`:

```
pin           := pin_type [ "." pin_number ] [ ":" attribute ]*
pin_type      := "gpio" | "i2so" | "uart_channel" digit+ | "no_pin" | "void" | "pinext" digit   ("pinext" — DO NOT USE, see §3.5)
pin_number    := non-negative integer   (no board-legality check performed by this spec)
attribute     := active_state | pull | drive_strength
active_state  := "high" | "low"          (default: high)
pull          := "pu" | "pd"             (pull-up / pull-down; default: none/floating)
drive_strength:= "ds" digit              (digit 0-3; default 2 (~40mA); gpio output pins only)
special_value := "NO_PIN"                (canonical spelling of the no_pin type, no dot/number needed)
```

Rules:
- Attributes are colon-prefixed and may appear **in any order**, chained: `gpio.16:low:pu`.
- Pin-type matching is case-insensitive (`string_util::equal_ignore_case`/`starts_with_ignore_case` throughout `Pin::parse()`), consistent with §0.8 — `GPIO.16`, `Gpio.16`, and `gpio.16` are identical.
- `NO_PIN` is the universal "no pin assigned" sentinel and is the default for essentially every pin field. It is **not** written as `gpio.NO_PIN` — it stands alone, with no dot or number (confirmed: `Pin::parse()` matches the whole pre-dot token against `"no_pin"`, and a string with no `.` in it becomes that whole token per `string_util::split_prefix()`).

### 3.1 `gpio.N` — native MCU pin
Usable for input or output depending on the feature (subject to real hardware constraints this spec does not check). FluidNC's primary target is the ESP32, but a development build also runs on RP2040 — `gpio.N` is the same generic pin-type prefix on both, just mapped to whichever MCU's numbering scheme the build targets; nothing in this spec's `gpio.N` grammar is ESP32-specific. Duplicate-use detection (§0.12) is tracked separately from every other pin type.

### 3.2 `i2so.N` — output-only pin on an external I2S shift-register chain
Only valid on boards that implement I2SO hardware (compiled in only when `MAX_N_I2SO` is set); cannot be used for PWM. Case in practice is often written `I2SO.5` (uppercase) in real configs — both cases are equivalent per the case-insensitivity above; prefer lowercase `i2so.5` for consistency with the wiki's canonical examples. Duplicate-use detection is tracked separately from `gpio.N` — a config can legally use `gpio.5` and `i2so.5` simultaneously; the number doesn't collide across types.

### 3.3 `uart_channelN.M` — virtual I/O pin carried over a UART "channel" to remote/companion hardware
Advanced/rare: this treats a byte-stream UART connection as a set of virtual GPIO-like pins by encoding pin state changes as bytes written to the channel (`ChannelPinDetail`, `Pins/ChannelPinDetail.cpp`). `N` selects which `uart_channelN:` top-level section (see §9.1) to use; `M` is the virtual pin index on that channel, not a real GPIO number. Supports the same `:pu`/`:pd`/`:low`/`:high` attributes as `gpio`/`i2so` pins (`ds<N>` drive-strength does not apply, since there's no real analog output stage).

### 3.4 `void` — a real, allocatable pin implementation that does nothing
Distinct from `NO_PIN`: `void` constructs an actual `Pins::VoidPinDetail` object (accepts writes, reports reads, participates as a normal pin from the rest of the firmware's point of view) but drives no physical hardware. Written bare, with **no** number (`void`, not `void.0`) — same reasoning as `NO_PIN`, the whole pre-dot token is matched literally against `"void"`. Unlike every other pin type, **`void` is explicitly exempt from the duplicate-pin-use check** (§0.12) — the source comment notes *"having multiple void pins has its uses for debugging."* Mainly useful for temporarily disabling one part of a machine (e.g. one axis's limit switch) without leaving a pin field completely unconfigured, or for debugging/bench-testing a config with no hardware attached at all.

### 3.5 `pinextN.M` — a pin on an I2C GPIO-expander chip configured under `extenders:` — **DO NOT USE (provisional, may be removed)**
**This entire feature (pin extenders / `pinextN.M` / `extenders:` / `pinextenderN:`) is provisional in FluidNC and may be removed in a future version.** A generator should not emit configs using this feature; it's documented here only so that if it's encountered in an existing config, it can be recognized and understood, not because it should be generated. If you need spare I/O, prefer `user_outputs:`/`user_inputs:` (§15) or native `gpio.N` pins instead.

**Naming trap (if you do encounter this in the wild):** the pin-string prefix (`pinext0`, `pinext1`, ... `pinext9`) is *not* the same string as the config section name that defines the expander (`pinextender0:`, `pinextender1:`, ... under `extenders:` — see §19). `pinext` (used in pin strings) and `pinextender` (used as a section key) are genuinely different literal strings in the source (`Pins/ExtPinDetail.cpp` vs `Extenders/Extenders.cpp`). `N` (single digit, 0-9) selects which `pinextenderN:` block configured the chip; `M` is the port/pin number on that expander chip.
```yaml
extenders:
  pinextender0:
    pca9539:
      busId: 0

# ...elsewhere, referencing a pin ON that expander:
some_output_pin: pinext0.5     # NOT pinextender0.5
```

Example forms actually seen in real configs, across the four types that should actually be generated:
```yaml
step_pin: gpio.12
direction_pin: I2SO.4
limit_all_pin: gpio.16:low:pu
flood_pin: gpio.25:high
mosi_pin: gpio.23:ds1
enable_pin: NO_PIN
debug_pin: void
remote_io_pin: uart_channel1.3:low
```
(`pinextN.M` intentionally omitted from this list — do not use, see above.)

### 3.6 `i2so:` section (required if any `i2so.N` pin is used anywhere in the file)

```yaml
i2so:
  bck_pin: gpio.22
  data_pin: gpio.21
  ws_pin: gpio.17
  min_pulse_us: 2      # Integer, one of {1, 2, 4}, default 2
```
If any `i2so.N` pin appears anywhere in the config and this section is missing, the i2so pins will not function.

---

## 4. `stepping:` section

Top-level, singular, governs global step-pulse generation for **all** motors. Ground truth: `Stepping::group()` (`Stepping.cpp`) and the static default initializers in the same file.

```yaml
stepping:
  engine: RMT                # Enum: Timed | RMT | I2S_STATIC | I2S_STREAM, default is board-dependent (DEFAULT_STEPPING_ENGINE)
  idle_ms: 255                # Integer 0-10000000, default 255 ("always enabled" — see note below)
  pulse_us: 4                 # Integer 0-30, default 4
  dir_delay_us: 0              # Integer 0-10, default 0
  disable_delay_us: 0          # Integer 0-1000000, default 0
  segments: 12                 # Integer 6-20, default 12
```

Rules / common mistakes:
- **`idle_ms` default is 255, not some ordinary millisecond value** — 255 is *both* the out-of-the-box firmware default *and* the Grbl-compatibility magic value meaning "never auto-disable motors." So an unconfigured `stepping:` section already leaves motors permanently enabled; any other value 0–254 or 256+ is a real millisecond delay before auto-disable. Get this backwards (e.g. assuming the default is some small idle timeout) and a generated config will silently behave as always-enabled unless `idle_ms` is deliberately set otherwise.
- `engine:` enum values are exactly `Timed`, `RMT`, `I2S_STATIC`, `I2S_STREAM` (case-insensitive per §0.8; canonical display spelling is `Timed`, not `TIMED`). `RMT` is only compiled in when the board supports it (`MAX_N_RMT`), and `I2S_STATIC`/`I2S_STREAM` only when `MAX_N_I2SO` is set — an engine value valid on one board build may not exist on another. `I2S_STATIC` and `I2S_STREAM` are functionally identical; the two names are only historical. **Do not mix `i2so.N` pins into a config using `engine: Timed` or `engine: RMT`** — i2so pins require an I2S stepping engine.
- `pulse_us`'s real ceiling is 30, not the tighter 10 this document previously (incorrectly, wiki-sourced) stated — but keep in mind §5.3's rate-limit math still applies: the firmware checks `1000000 / ((2 * pulse_us) + dir_delay_us)` against the required step rate at load time and will throw an initialization error such as `Stepping rate N steps/sec exceeds the maximum rate M` if `steps_per_mm * max_rate_mm_per_min / 60` exceeds what `pulse_us`/`dir_delay_us` can physically support. Keep `steps_per_mm` no higher than needed (lower microstepping if margin is tight), even though the field itself now permits values up to 30.

---

## 5. `axes:` section — now fully re-verified against `Machine/Axis.cpp`, `Machine/Motor.cpp`, `Machine/Homing.h`, and every file under `Motors/`, closing the standing gap noted in earlier passes. Numerous real corrections were found and are called out inline below (wrong ranges, wrong defaults, missing fields, and one case — §5.4.7 — where three differently-named driver types turned out to be functionally identical).

### 5.1 Axis-group-level keys (direct children of `axes:`, siblings of axis letters)

```yaml
axes:
  shared_stepper_disable_pin: NO_PIN   # Pin, gpio or i2so, default NO_PIN
  shared_stepper_reset_pin: NO_PIN     # Pin, gpio or i2so, default NO_PIN
  homing_runs: 2                        # Integer 1-5, default 2

  x:
    # ... axis letter block, see 5.2
```

### 5.2 Axis letter blocks

- Valid axis letter keys: `x`, `y`, `z`, `a`, `b`, `c`. As established in §0.13, these can be defined **in any order in the file** — matching is by name, not textual position. The real pitfall is different: whichever axis has the *highest index* actually defined in the file determines how many axes exist, and any lower-indexed axis you didn't explicitly define gets silently created with all-default values rather than being absent. So defining `z:` without `x:`/`y:` still gives you 3 axes — `x`/`y` just end up configured however the defaults happen to be, not the way you intended. Minimum 3 axes must exist; always explicitly define at least `x`, `y`, `z`.
- `x`, `y`, `z` are linear axes and report in inches under G20. `a`, `b`, `c` are treated as rotary/universal-unit axes and never scale for inches, even if used as a physical linear axis.

```yaml
axes:
  x:
    steps_per_mm: 80.000              # Float, 0.001-100000.000, default 80.000
    max_rate_mm_per_min: 1000.000     # Float, 0.001-250000.0, default 1000.000
    acceleration_mm_per_sec2: 25.000  # Float, 0.001-100000.000, default 25.000
    max_travel_mm: 1000.000           # Float, 0.1-10000000.0, default 1000.000
    soft_limits: false                # Boolean, default false
    idle_disable: true                # Boolean, default true (child of soft_limits in docs, but written as sibling key)

    homing:
      # see 5.3

    motor0:
      # see 5.4 — required for a working axis (motor1: optional, second motor)
    motor1:
      null_motor:              # explicit "no second motor" — see 5.4.10
```

Common mistakes:
- `soft_limits: true` requires homing to be meaningful/safe — always home before relying on soft limits.
- `max_travel_mm` is measured from the pulled-off homing switch position, not from an arbitrary datum — don't just copy a machine's physical travel spec without accounting for pull-off.

### 5.3 `homing:` block (child of an axis letter)

Ground truth: `Homing::group()` (`Homing.h`). Several previously-documented ranges/defaults were wrong — corrected here.

```yaml
homing:
  cycle: 0                     # Integer -1 to MAX_N_AXIS (board-dependent, typically 6), default 0. See semantics below.
  allow_single_axis: true      # Boolean, default true
  positive_direction: true     # Boolean, default true
  mpos_mm: 0.000                # Float, UNBOUNDED (no min/max enforced), default 0.000
  seek_mm_per_min: 200.000      # Float, 1.0-100000.0, default 200.000
  feed_mm_per_min: 50.000       # Float, 1.0-100000.0, default 50.000
  settle_ms: 250                 # Integer, 0-1000, default 250
  seek_scaler: 1.1               # Float, 1.0-100.0, default 1.1
  feed_scaler: 1.1               # Float, 1.0-100.0, default 1.1
```

`cycle:` semantics (get this exactly right — it's a frequent LLM error to treat this as a simple boolean):
- `cycle: -1` (the named constant `set_mpos_only` in source) — axis does **not** physically move during homing; its `mpos_mm` is just assigned directly. Used for axes with no home switch.
- `cycle: 0` (**the actual default** — corrected from an earlier, wrong claim that -1 was the default) — axis does not home with `$H` (home-all), but *can* still be homed individually with `$H<axis>` (assuming `allow_single_axis` stays true, which it does by default). **Practical consequence: an axis whose `homing:` block is left entirely at defaults is individually homeable but silently excluded from group `$H`** — a generator relying on "just leave homing: at defaults" to get an axis included in `$H` will be wrong; `cycle:` must be explicitly set to 1 or higher for that.
- `cycle: 1` or higher — axis homes as part of `$H`. Multiple axes sharing the same cycle number home simultaneously in that pass. Convention: Z on `cycle: 1` (home it first, alone), then X/Y together on `cycle: 2`.
- **Multi-axis homing (same cycle number for 2+ axes) cannot be used with CoreXY kinematics**, since CoreXY drives two motors per single logical axis move.
- If a motor uses `limit_all_pin` (single shared switch for both travel directions), that switch must be manually cleared before homing — the firmware cannot know which direction to back off.

### 5.4 `motorN:` blocks (child of an axis letter; `motor0:` and optionally `motor1:`)

Shared motor-level (not driver-type-specific) keys — confirmed exact against `Motor::group()` (`Motor.cpp`):

```yaml
motor0:
  limit_neg_pin: NO_PIN     # Pin (input), gpio, default NO_PIN
  limit_pos_pin: NO_PIN     # Pin (input), gpio, default NO_PIN
  limit_all_pin: NO_PIN     # Pin (input), gpio, default NO_PIN — mutually exclusive with the two above
  hard_limits: false        # Boolean, default false
  pulloff_mm: 1.000         # Float, 0.1-100000.0, default 1.000

  <driver_type>:             # exactly one driver-type block, dispatched via MotorFactory::factory() — see 5.4.1 - 5.4.11
    ...
```

Rule: **never specify both `limit_all_pin` and (`limit_neg_pin` or `limit_pos_pin`) on the same motor.** Use `limit_all_pin` only when both ends share one physical switch/circuit.

Below each motor-level block sits **exactly one** driver-type key naming the actual hardware. This whole subsection has now been fully re-verified against `Motors/*.cpp`/`.h` (was previously wiki-only); several real corrections are called out below.

#### 5.4.1 `standard_stepper:` — generic external step/dir/enable driver

Ground truth: `StandardStepper::group()`. This is also the field set that every other driver type below inherits as its base — `step_pin`/`direction_pin`/`disable_pin` are not separately redefined by TMC/StepStick drivers, they're literally this same base class's fields.
```yaml
standard_stepper:
  step_pin: NO_PIN         # Pin (output), gpio or i2so
  direction_pin: NO_PIN    # Pin (output), gpio or i2so
  disable_pin: NO_PIN      # Pin (output), gpio or i2so
```

#### 5.4.2 `stepstick:` — driver socket with microstep-select pins (DRV8825/A4988/TB67S249FTG family)

Ground truth: `StepStick::group()`, confirmed to call `StandardStepper::group()` first (inherits `step_pin`/`direction_pin`/`disable_pin`) then adds:
```yaml
stepstick:
  step_pin: NO_PIN
  direction_pin: NO_PIN
  disable_pin: NO_PIN
  ms1_pin: NO_PIN           # Pin, gpio or i2so, default NO_PIN
  ms2_pin: NO_PIN
  ms3_pin: NO_PIN
  reset_pin: NO_PIN         # sets state at boot only, no active runtime behavior
```

**A TMC2209 driven in standalone mode is a `stepstick:`, not a `tmc_2209:`.** `tmc_2209:` (§5.4.6) is specifically for driving the chip's UART register interface — if the UART isn't actually wired up and configured, that section's fields (`run_amps`, `microsteps`, `run_mode`, etc.) have nothing to act on. This applies in two real situations: (1) a TMC2209 soldered onto the controller board itself with no UART connection broken out to it, and (2) an off-the-shelf TMC2209 stepstick module that's factory-jumpered for standalone/legacy mode (MS1/MS2 pins tied to set microstepping directly rather than left free for UART addressing). In both cases, configure it as `stepstick:` and control microstepping via `ms1_pin`/`ms2_pin`/`ms3_pin` (or the module's physical jumpers) exactly as you would for a DRV8825/A4988 — current is then set by the module's onboard trimpot, not by any config field.

#### 5.4.3 `tmc_2130:` — SPI-controlled Trinamic driver

Class chain: `StandardStepper → TrinamicBase → TrinamicSpiDriver → TMC2130Driver`. Ground truth: `TrinamicBase::group()` + `TrinamicSpiDriver::group()` (TMC2130Driver.h adds nothing further). **Two real corrections from the previous pass: `r_sense_ohms` minimum is 0.0, not 0.01; and `homing_amps` does NOT exist on this driver type at all (it was previously, incorrectly, listed here).**
```yaml
tmc_2130:
  step_pin: NO_PIN
  direction_pin: NO_PIN
  disable_pin: NO_PIN
  cs_pin: NO_PIN             # Pin (output). Independent SPI mode: each driver needs its own. Daisy chain: only define on spi_index:1 motor.
  spi_index: -1               # Integer -1 to 127. -1 = independent mode (all drivers use -1). Daisy chain: 1,2,3... in chain order.
  r_sense_ohms: 0.11          # Float 0.0-1.00, default 0.0 (TMC2130Driver.h sets no override; set a real value — see note below)
  run_amps: 0.5                # Float 0.05-10.0, default 0.5
  hold_amps: 0.5               # Float 0.05-10.0, default 0.5
  microsteps: 16                # Integer 1-256, default 16
  stallguard: 0                 # Integer -64 to 63, default 0
  stallguard_debug: false       # Boolean, default false
  toff_disable: 0                # Integer 0-15, default 0
  toff_stealthchop: 5            # Integer 2-15, default 5
  toff_coolstep: 3               # Integer 2-15, default 3
  run_mode: StealthChop           # Enum: StealthChop | CoolStep | StallGuard, default StealthChop
  homing_mode: StealthChop        # Enum: StealthChop | CoolStep | StallGuard, default StealthChop
  use_enable: false                # Boolean, default false
  diag0_error: false               # Boolean, default false
  diag0_otpw: false                # Boolean, default false
  diag0_int_pushpull: false        # Boolean, default false
```
**`r_sense_ohms` has no real per-chip default baked into the config item itself (`TrinamicBase::_r_sense` defaults to `0`, a non-functional value)** — a real value appropriate to the physical module must always be supplied explicitly; `0.11` is the typical value for genuine TMC2130 modules but is a convention to follow, not a firmware default you can omit. The enum's third value is spelled `StallGuard` (capital G), not `Stallguard` as previously written — case-insensitive matching (§0.8) means either works, but `StallGuard` is the canonical spelling shown in source.

Daisy-chain rule: in a daisy chain, FluidNC must know about **every** driver in the physical chain, even unused positions — you must define a motor entry for each chain position, using placeholder/dummy values for positions with no real axis if necessary, or the chain's data alignment breaks.

#### 5.4.4 `tmc_2208:` — UART-*capable* but commonly standalone STEP/DIR Trinamic driver; **not individually addressable**

Class chain: `StandardStepper → TrinamicBase → TrinamicUartDriver → TMC2208Driver`. Ground truth confirms the field set below, but corrects the previous documentation's invented per-motor `uart:` sub-block — **that sub-block does not exist for TMC2208 either**; TMC2208 uses the same external `uart_num:` + top-level `uartN:` form as TMC2209 (§9), not a nested block. **Correction from real-world config testing:** an earlier pass of this document wrongly claimed `toff_disable`/`toff_stealthchop` don't exist on UART drivers — they do, inherited via `TrinamicBase::group()`, which both SPI and UART driver families share. Only `diag0_*` is genuinely SPI-only.
```yaml
tmc_2208:
  step_pin: NO_PIN
  direction_pin: NO_PIN
  disable_pin: NO_PIN        # or use_enable: true
  uart_num: 1                  # Integer — references a top-level uartN: section, see §9
  addr: 0                        # Integer — present on TrinamicUartDriver but not functionally meaningful for TMC2208 (chip has no hardware address pins)
  cs_pin: NO_PIN                  # rarely used; present on TrinamicUartDriver base
  r_sense_ohms: 0.11
  run_amps: 0.5
  hold_amps: 0.5
  microsteps: 16
  toff_disable: 0                  # Integer 0-15, default 0 -- inherited via TrinamicBase, present here too
  toff_stealthchop: 5              # Integer 2-15, default 5 -- inherited via TrinamicBase, present here too
  run_mode: StealthChop
  homing_mode: StealthChop
  stallguard: 0                    # Integer -64 to 63, default 0
  stallguard_debug: false
  toff_coolstep: 3
  use_enable: false
```
`diag0_error`/`diag0_otpw`/`diag0_int_pushpull` are **not** present on TMC2208 (or TMC2209) — those genuinely are SPI-driver-only fields (`TrinamicSpiDriver::group()`), not part of `TrinamicUartDriver::group()`.

**Critical daisy-chain gotcha:** TMC2208 chips are not individually addressable over UART (the `addr:` field is inherited from the shared UART-driver base class but the chip itself has no hardware address pins to make use of it). If multiple TMC2208 motors share one UART bus, the register values (run_amps, hold_amps, microsteps, etc.) actually applied at runtime are **whichever motor/axis is defined LAST in the file** — earlier per-motor values for those fields are silently overridden, and any field *not* set on that last motor falls back to firmware default (not to what an earlier motor specified). Do not assume distinct per-motor current/microstepping settings will actually take effect in a TMC2208 daisy chain.

#### 5.4.5 `tmc_5160:` — SPI-controlled, higher-current Trinamic driver

Class chain: `StandardStepper → TrinamicBase → TrinamicSpiDriver → TMC5160Driver`. Confirmed: identical key set to `tmc_2130` (same `TrinamicSpiDriver::group()` base) plus one addition:
```yaml
tmc_5160:
  # ...all tmc_2130 keys (step_pin through diag0_int_pushpull)...
  tpfd: 4                      # Integer 0-15, default 4
```
`r_sense_ohms` is typically `0.075` for genuine TMC5160 modules (vs `0.11` typical for TMC2130) — same "no real firmware default" caveat as §5.4.3 applies; always set it explicitly rather than omitting it.

#### 5.4.6 `tmc_2209:` — UART-controlled, individually addressable Trinamic driver

Class chain: `StandardStepper → TrinamicBase → TrinamicUartDriver → TMC2209Driver`. **Real corrections: `homing_amps` genuinely exists here (it's TMC2209-specific, not shared with 2130/2208/5160 as previously implied); `stallguard`'s range is different from every SPI-driver type — 0 to 255, not -64 to 63; and `toff_disable`/`toff_stealthchop` (missing from an earlier pass) ARE present here too, inherited via `TrinamicBase::group()`.**
```yaml
tmc_2209:
  uart_num: 1                  # Integer — refers to a top-level uartN: section, see §9
  addr: 0                       # Integer 0-3 — hardware-set via MS1/MS2 address pins on the chip
  cs_pin: NO_PIN                # usually NO_PIN for UART mode; only used if using cs_pin-based UART switching
  step_pin: NO_PIN
  direction_pin: NO_PIN
  disable_pin: NO_PIN           # or use_enable: true
  r_sense_ohms: 0.11
  run_amps: 1.0
  hold_amps: 0.5
  homing_amps: 0.0              # Float 0.0-10.0, default 0.0 — special behavior: if left at 0, it's silently set equal to run_amps at parse time (see note)
  microsteps: 16
  toff_disable: 0                # Integer 0-15, default 0
  toff_stealthchop: 5            # Integer 2-15, default 5
  run_mode: StealthChop
  homing_mode: StealthChop
  stallguard: 0                  # Integer 0-255 (NOT -64 to 63 — TMC2209 differs from every SPI driver type here), default 0
  stallguard_debug: false
  toff_coolstep: 3
  use_enable: false
```
Rules:
- **Only use `tmc_2209:` when the chip's UART is actually wired up and being addressed.** A TMC2209 without real UART wiring — whether it's an onboard chip with no UART broken out, or a stepstick module factory-jumpered for standalone mode — must be configured as `stepstick:` (§5.4.2) instead; `tmc_2209:`'s current/microstepping/mode fields do nothing without a working UART link to actually write them to the chip.
- **`homing_amps` has a real, useful fallback behavior worth knowing:** `TMC2209Driver::afterParse()` explicitly sets `_homing_current = _run_current` whenever `homing_amps` was left at its default of `0`. So omitting `homing_amps` entirely is equivalent to setting it equal to `run_amps` — a reasonable default, but only for this one driver type; don't assume the same fallback applies elsewhere (it doesn't — `homing_amps` doesn't exist as a field on any other TMC type in this document).
- Unlike TMC2208, TMC2209 chips **are** individually addressable, but only when each chip's hardware address pins (`MS1_AD0`/`MS2_AD1`) are wired to give it a unique `addr:` (0–3) on the shared UART bus — up to 4 chips per UART.
- `uart_num:` must reference a `uartN:` top-level section (§0.11 — no ordering requirement, but placing it earlier is still good practice).
- There is no per-motor nested `uart:` sub-block for TMC2209 (or TMC2208, per the correction in §5.4.4) — always use the external `uart_num:` + top-level `uartN:` section form.

#### 5.4.7 `tmc_5160Pro:` / `tmc_2160Pro:` / `tmc_2160:` — expert/raw-register mode

Ground truth: `TMC5160ProDriver::group()`. **A genuinely surprising finding from source: all three of these names are effectively the same driver.** `tmc_5160Pro` registers `TMC5160ProDriver` directly; `tmc_2160Pro` registers a subclass `TMC2160Driver` that adds *nothing* on top of `TMC5160ProDriver` (empty class body, differs only in an unused reference constant); and **`tmc_2160` (no "Pro" suffix) also registers that exact same `TMC2160Driver` class** — there is no separate "friendly semantic-field" TMC2160 driver the way `tmc_5160`/`tmc_2130` are for their respective chips. All three names — `tmc_5160Pro`, `tmc_2160Pro`, `tmc_2160` — are functionally identical raw-register drivers; pick whichever name best documents the actual chip for a human reading the config, since it makes no difference to the firmware. Also note the SPI `tpfd` field is present in source but **commented out** (`//handler.item("tpfd"...)`) — it is not an active config key on this driver despite appearing in the code.
```yaml
tmc_5160Pro:      # tmc_2160Pro: and tmc_2160: are functionally identical — see note above
  step_pin: NO_PIN
  direction_pin: NO_PIN
  disable_pin: NO_PIN
  cs_pin: NO_PIN
  spi_index: -1                # Integer -1 to 127, default -1
  use_enable: false
  CHOPCONF: 322994520           # Integer (uint32_t), default 322994520 — raw register value, consult TMC5160 datasheet
  COOLCONF: 0                     # Integer, default 0
  THIGH: 0                          # Integer, default 0
  TCOOLTHRS: 0                        # Integer, default 0
  GCONF: 4                              # Integer, default 4
  PWMCONF: 3289120798                     # Integer (uint32_t), default 3289120798
  IHOLD_IRUN: 7948                          # Integer, default 7948
```
These are raw 32-bit register values, not semantic fields — do not attempt to infer or invent register values beyond the shown defaults; if generating this block for a real, different chip configuration, either take values directly from a working example the user supplies, or flag that the user must fill in datasheet-derived values.

#### 5.4.8 `rc_servo:` — hobby RC servo used as a virtual linear/rotary axis

Class chain: `Servo → RcServo` (`Servo::group()` is empty, contributes no additional fields). Confirmed against `RcServo.h` and its `RcServoSettings.h` range constants.
```yaml
rc_servo:
  output_pin: NO_PIN      # Pin (output), gpio
  pwm_hz: 50                # Integer 50-200, default 50
  min_pulse_us: 1000        # Integer 500-2500, default 1000
  max_pulse_us: 2000        # Integer 500-2500, default 2000
  timer_ms: 20                # Integer 20-250, default 20
```
The servo's physical rotation range maps onto `max_travel_mm` of the enclosing axis. To reverse direction, swap `min_pulse_us`/`max_pulse_us` rather than inverting any pin attribute. `soft_limits: true` is strongly recommended on any axis using `rc_servo`.

#### 5.4.9 `solenoid:` — on/off (or two-level pull/hold) actuator used as a virtual axis

Class chain: `Servo → Solenoid`. **Two corrections from the previous pass: `pull_ms` default is 500, not 75; and there's a `timer_ms` field that was missing entirely.**
```yaml
solenoid:
  output_pin: NO_PIN
  pwm_hz: 1000              # Integer 1000-100000, default 1000
  off_percent: 0.0            # Float 0-100, default 0.0
  pull_percent: 100.0         # Float 0-100, default 100.0
  hold_percent: 75.0          # Float 0-100, default 75.0
  pull_ms: 500                  # Integer 0-3000, default 500 (corrected — previously wrongly stated as 75)
  direction_invert: false        # Boolean — inverts which side of mpos 0.0 is "active", default false
  timer_ms: 50                     # Integer, default 50 — previously undocumented field
```

#### 5.4.10 `dynamixel2:` — Robotis Protocol 2 servo

Class chain: `Servo → Dynamixel2`. **Corrections: `id` default is 255, not 1; `count_min`/`count_max` defaults are 1024/3072, not 0/4095; and a `timer_ms` field was missing entirely.**
```yaml
dynamixel2:
  uart_num: 1            # Integer — references top-level uartN: section; baud should match servo's programmed baud (1000000 recommended), mode "8N1" (quote it, see §2)
  id: 255                 # Integer, default 255 — must be set to a real, unique servo ID per device on the bus; 255 is Dynamixel's broadcast address and not a usable per-device value
  count_min: 1024           # Integer, default 1024 — servo raw count at the low end of axis mpos range
  count_max: 3072             # Integer, default 3072 — servo raw count at the high end of axis mpos range
  timer_ms: 50                  # Integer, default 50 — previously undocumented field
```
Swap `count_min`/`count_max` to reverse direction. Given the default `id: 255` is Dynamixel's broadcast address rather than a real per-servo ID, always set `id:` explicitly to the actual configured address of the physical servo.

#### 5.4.11 `null_motor:`

```yaml
motor1:
  null_motor:
```
Explicitly declares "this motor slot is unused." Takes no sub-keys. Used for `motor1:` on single-motor axes, and required as a placeholder in some daisy-chain scenarios. Also confirmed as the automatic fallback: `Motor::afterParse()` constructs a `null_motor` itself whenever no driver-type key was given at all, so an entirely empty `motor1: {}` (or omitting `motor1:` altogether — though the section is always registered per §5.1) behaves the same as writing `null_motor:` explicitly.

---
## 6. `spi:` and `sdcard:` sections (only needed if using an SD card)

```yaml
spi:
  miso_pin: gpio.19       # must be a native GPIO with input capability
  mosi_pin: gpio.23       # must be a native GPIO with output capability
  sck_pin: gpio.18        # must be a native GPIO with output capability

sdcard:
  cs_pin: NO_PIN                 # Pin (output) — required (non-NO_PIN) for SD to function
  card_detect_pin: NO_PIN        # Pin (input), optional, cosmetic only (shown in startup log)
  frequency_hz: 8000000           # Integer, 400000-20000000, default 8000000
```
Both `spi:` and `sdcard:` are required together — defining one without the other leaves the SD card non-functional.

---

## 7. `control:` section — physical input buttons/switches

Ground truth: `Control::Control()` constructor (`Control.cpp`), which populates a data-driven list of `(event, key_name, report_letter)` triples rather than static `handler.item()` calls — confirms this section exactly as previously documented, with one extra non-obvious detail. All fields are Pins (input), gpio only, default `NO_PIN`:

```yaml
control:
  safety_door_pin: NO_PIN
  reset_pin: NO_PIN
  feed_hold_pin: NO_PIN
  cycle_start_pin: NO_PIN
  macro0_pin: NO_PIN
  macro1_pin: NO_PIN
  macro2_pin: NO_PIN
  macro3_pin: NO_PIN
  fault_pin: NO_PIN
  estop_pin: NO_PIN
  homing_button_pin: NO_PIN
```
Rule: **every control input must read as inactive at boot.** If wired backwards (reads active at startup), the firmware raises an "active at startup" alarm — fix by flipping the pin's `:high`/`:low` attribute (§3), not by changing anything in this section's key structure.

**Non-obvious detail from source:** `estop_pin` and `fault_pin` are registered against the *identical* internal event (`&faultPinEvent`) — they are not functionally distinct control inputs, just two named config keys wired to the same behavior. Wiring both simultaneously is legal (each is just another edge-triggered instance of the same event) but doesn't give you two independent behaviors; don't assume `estop_pin` has emergency-stop-specific semantics beyond what `fault_pin` already does.

(Also, for the avoidance of doubt given §0.13's finding on `axes:`: the C++ constructor comment noting `safety_door_pin` "must be defined first" refers to internal construction-order priority in `_pins`, not YAML key order in the config file — key matching here, as everywhere else, is by name, not file position.)

---

## 8. `coolant:` section

Ground truth: `CoolantControl::group()` (`CoolantControl.cpp`) — confirms this section exactly as previously documented from the wiki.
```yaml
coolant:
  mist_pin: NO_PIN      # Pin (output), gpio or i2so — controlled by M7 / M9
  flood_pin: NO_PIN     # Pin (output), gpio or i2so — controlled by M8 / M9
  delay_ms: 0             # Integer 0-10000, default 0 — only delays turn-ON, never delays M9 (off)
```

### 8.1 `probe:` section

Ground truth: `Probe::group()` (`Probe.h`/`Probe.cpp`). This section had been referenced elsewhere in this document (e.g. the §12.1 worked example) but never actually given its own write-up until this pass — flagged here as a completeness gap that's now closed.
```yaml
probe:
  pin: NO_PIN                  # Pin (input, EventPin) — the touch probe input
  toolsetter_pin: NO_PIN         # Pin (input, EventPin) — a separate fixed toolsetter input, if present
  check_mode_start: true           # Boolean, default true — see note below
  hard_stop: false                   # Boolean, default false
  probe_hard_limit: false              # Boolean, default false — non-probing-motion protection
```
- `check_mode_start` governs behavior specifically during G38.2/G38.4 **check mode**: `false` sets the reported position to the probe's target after the cycle; `true` (the default) sets it back to the position the probe move started from.
- The section (and the probe feature as a whole) is considered present only if at least one of `pin`/`toolsetter_pin` is actually defined (`Probe::exists()` checks `_probePin.defined() || _toolsetterPin.defined()`) — an all-default `probe:` block with both pins left at `NO_PIN` is equivalent to omitting the section entirely.

---

## 9. `uart1:`, `uart2:`, ... top-level sections

Required whenever any driver/device references `uart_num: N`. Not indexed from a parent list — each is its own top-level key named `uart<N>:`. Ground truth: `Uart::group()` (`Uart.cpp`) — this section has grown two fields beyond what earlier passes of this document covered, plus an entirely separate factory-dispatched form.

```yaml
uart1:
  txd_pin: NO_PIN         # Pin (output), gpio
  rxd_pin: NO_PIN         # Pin (input), gpio
  rts_pin: NO_PIN         # Pin, optional, default NO_PIN
  cts_pin: NO_PIN         # Pin, optional, default NO_PIN
  baud: 115200              # Integer 2400-10000000, must match the device's programmed/expected baud
  mode: "8N1"                # UartData — always quote (see §2)
  passthrough_baud: 0          # Integer 0-10000000, default 0 — 0 means not configured
  passthrough_mode: "8N1"        # UartData — always quote
```
As established in §0.11, a `uartN:` section may legally appear anywhere in the file relative to whatever references it via `uart_num: N` — resolution happens in each component's `init()`, after the whole file is parsed. Placing it early is still good practice for human readability, not a parser requirement.

### 9.0.1 `usb_host:` — alternative form selecting USB host mode instead of hardware UART pins

`Uart::group()` first checks for a nested factory-dispatched subsection (the same `GenericFactory` pattern used by motor/spindle/kinematics type selection) before falling back to the plain fields above:
```yaml
uart3:
  usb_host:
    baud: 1000000
```
When `usb_host:` is present, none of the plain `txd_pin`/`rxd_pin`/etc. fields apply — this uses the board's USB-OTG/host peripheral (e.g. for a USB pendant) instead of physical UART pins. **Caveat:** the concrete `usb_host` implementation was not found in the source snapshot checked for this pass (only the factory dispatch point in `Uart.cpp`, its explanatory comment, and one real `example_configs/usb_host_pendant.yaml` were) — only `baud` is confirmed; if other fields exist they are undocumented here. This looks like either a very recently landed or not-yet-merged feature; re-verify against a current checkout before relying on more than `baud`.

### 9.1 `uart_channel1:`, `uart_channel2:`, ... top-level sections

Ground truth: `UartChannel.h`. A distinct, higher-level concept from `uartN:` above — a `uart_channelN:` section wraps a physical `uartN:` bus with logging/reporting behavior and makes it addressable as a virtual pin source (`uart_channelN.M` pin strings, §3.3).

```yaml
uart_channel1:
  uart_num: 1                      # Integer — references the underlying uartN: physical bus
  report_interval_ms: 0              # Integer, default 0
  message_level: Info                  # Enum: None | Error | Warn | Info | Debug | Verbose, default None
```
Advanced/rare feature (companion-controller / remote-IO style setups); most configs will never need this section.

**Note:** the official `example_configs/uartio.yaml` includes an `all_messages: false` key here. This is **not a valid config item** — confirmed not present in `UartChannel::group()`, and that example file is simply stale. Do not generate `all_messages:`; only the three fields above are real.

---

## 10. Spindle sections

FluidNC allows **one or more** spindle definitions, each named by its driver-type key (not by a generic `spindle:` wrapper key). At least one is expected for a functioning machine; if none is specified, FluidNC synthesizes a default `NoSpindle:`.

### 10.1 Speed Map (used by every spindle type except `NoSpindle:`)

**Semantically, `speed_map` is a piecewise-linear mapping from GCode S-word values to a percentage of the spindle's device-specific maximum output units** (PWM duty, DAC level, VFD RPM range, etc., depending on spindle type) — each entry is one (S-value, percent) breakpoint, and FluidNC linearly interpolates between consecutive breakpoints at runtime to convert an arbitrary commanded `S` value into the actual output level.

Exact grammar, ground truth `Parser::speedEntryValue()` (`Configuration/Parser.cpp`):
```
speed_map := entry (" "+ entry)*
entry      := s_value "=" percent "%"
s_value    := non-negative integer            (parsed via from_decimal into an unsigned SpindleSpeed — same integer-only rule as §2's Integer type; no decimals, no negative values)
percent    := float                             (parsed via from_float — decimals allowed, e.g. 100.000)
```
Confirmed parsing details worth knowing:
- Entries are split on whitespace, so any run of one-or-more spaces between entries works, not just exactly one (consistent with §0.3's general indentation/whitespace flexibility elsewhere in the file).
- Each entry is then split on the **first** `=` and the **first** `%` — so `s_value` must be a bare integer (no `=` or `%` characters in it) and `percent` must be a bare (possibly decimal) number.
- **The trailing `%` is not actually enforced by the parser** — the code finds `%` if present and strips it, but if it's absent, the remaining substring is still handed to the float parser and succeeds as long as it's a valid number. Despite this, always write the `%` — it's the documented/conventional form throughout FluidNC's own examples and this document, and omitting it is an unnecessary way to make a config file harder for a human to read for no parsing benefit.
- If the whole `speed_map` value is empty or fails to parse, FluidNC logs `"Using default speed map"` and falls back to a built-in default rather than erroring the file load.
- S-values should be monotonically increasing (first entry typically `0=...%`, representing off/minimum; last entry typically the maximum expected `S` value at `100.000%`) — the parser doesn't enforce ordering, but non-monotonic entries will produce nonsensical interpolation at runtime.

Example: `speed_map: 0=0.000% 1000=0.000% 24000=100.000%` — space-separated `S_value=percent%` pairs. No commas between entries.

### 10.2 Class hierarchy and fields common to most spindle types (verified against `Spindles/*.h` source)

Every spindle type derives, directly or indirectly, from `Spindle`, whose `group()` always contributes:
```yaml
tool_num: 0                      # Integer 0-255 (MaxToolNumber), default 0 — see §10.12 for multi-spindle tool ranges
speed_map: "0=0.000% 10000=100.000%"   # Speed Map, see §10.1
off_on_alarm: false               # Boolean, default false
atc:                               # String, optional — names an ATC block, see §16
m6_macro:                          # Macro (single line), optional
s0_with_disable: true              # Boolean, default true
disable_with_s0: false              # Boolean, default false
spinup_ms: 0                         # Integer 0-60000, default 0 — ONLY present if the type's use_delay_settings() is true (true for every type below except PlasmaSpindle)
spindown_ms: 0                        # Integer 0-60000, default 0 — same condition as spinup_ms
```

Most (but not all) spindle types are further built on `OnOff` (adds `direction_pin` + `groupCommon`: `output_pin`, `enable_pin`, then `Spindle::group()`), and `PWM` extends `OnOff` by adding `pwm_hz` on top. Knowing this inheritance chain matters because it explains real field-set differences below — e.g. `Dac` inherits from `OnOff`, **not** `PWM`, so it has no `pwm_hz`.

### 10.3 `PWM:` — the common case (single PWM output, direction pin, enable pin)
Chain: `Spindle → OnOff → PWM`.
```yaml
PWM:
  pwm_hz: 5000              # Integer 1-20000000, default 5000
  direction_pin: NO_PIN     # M4 only works if this is a real pin
  output_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 10000=100.000%"
  off_on_alarm: false
  s0_with_disable: true
  disable_with_s0: false
  spinup_ms: 0
  spindown_ms: 0
```

### 10.4 `10V:` — 0-10V control with separate forward/reverse pins
Chain: `Spindle → OnOff → PWM → _10v`. Adds `forward_pin`/`reverse_pin` on top of every `PWM:` field.
```yaml
10V:
  forward_pin: NO_PIN
  reverse_pin: NO_PIN
  pwm_hz: 5000
  direction_pin: NO_PIN
  output_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 1000=0.000% 24000=100.000%"
  off_on_alarm: false
```

### 10.5 `DAC:` — native ESP32 DAC (analog, **not** PWM) — genuinely ESP32-specific, unlike `gpio.N` (§3.1)
Chain: `Spindle → OnOff → Dac` (**not** via `PWM`, so no `pwm_hz`). `output_pin` is restricted to `gpio.25` or `gpio.26` only — hard-coded in `DacSpindle.cpp`'s init check (logs `"DAC spindle pin invalid ... (pin 25 or 26 only)"`), not something the parser itself enforces at load time, but it will log an error and the DAC won't function on any other pin. This is the one spindle type genuinely tied to ESP32-specific silicon (its onboard DAC peripheral) rather than being MCU-generic — expect this type to be unavailable on an RP2040 build of FluidNC.
```yaml
DAC:
  output_pin: gpio.25       # ONLY gpio.25 or gpio.26 are functional
  direction_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 255=100.000%"
  off_on_alarm: false
```

### 10.6 `HBridge:` — separate CW/CCW PWM outputs
Chain: `Spindle → HBridge` directly (does **not** go through `OnOff`, so no `direction_pin` — direction is inherent in which of the two outputs is driven).
```yaml
HBridge:
  pwm_hz: 5000
  output_cw_pin: NO_PIN
  output_ccw_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 10000=100.000%"
  off_on_alarm: false
```

### 10.7 `Laser:` — special M3/M4 semantics, only runs during feed-controlled motion
Chain: `Spindle → OnOff → Laser`, but Laser deliberately calls `OnOff::groupCommon()` (not the full `OnOff::group()`), so it explicitly has **no `direction_pin`**. `pwm_hz` range is narrower than plain `PWM:` (1000–100000, not 1–20000000).
```yaml
Laser:
  pwm_hz: 5000               # Integer 1000-100000, default 5000
  output_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 255=100.000%"
  off_on_alarm: true          # strongly recommended true for lasers, unlike the false default elsewhere
```

### 10.8 `Relay:` and `OnOff:` — full on/off, driven by any S>0
Chain: `Spindle → OnOff`, used with **no** further subclassing. Both `Relay` and `OnOff` register the identical `OnOff` class under two different factory names — they are functionally indistinguishable; use whichever name reads more clearly for the hardware in question. Note this is **not** a minimal field set — it carries the same fields as `PWM:` minus `pwm_hz`, including `speed_map` (used only to decide the S-value on/off threshold, not for real PWM duty).
```yaml
Relay:                        # or: OnOff:
  direction_pin: NO_PIN
  output_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 1=100.000%"
  off_on_alarm: false
```

### 10.9 `BESC:` — brushless ESC driven by RC-style pulse widths riding on a PWM carrier
Chain: `Spindle → OnOff → PWM → BESC`. Adds `min_pulse_us`/`max_pulse_us` on top of every `PWM:` field.
```yaml
BESC:
  min_pulse_us: 1000          # Integer 500-3000
  max_pulse_us: 2000          # Integer 500-3000
  pwm_hz: 5000
  direction_pin: NO_PIN
  output_pin: NO_PIN
  enable_pin: NO_PIN
  tool_num: 0
  speed_map: "0=0.000% 10000=100.000%"
  off_on_alarm: false
```

### 10.10 `PlasmaSpindle:` — experimental plasma-cutter torch control with arc-ok monitoring
Chain: `Spindle → PlasmaSpindle` directly. Distinct field set: **no `output_pin`** (torch firing is handled outside this section), adds `arc_ok_pin` (an event/input pin monitored for arc-loss) and `arc_wait_ms`. This type also overrides `use_delay_settings()` to `false`, so it does **not** get `spinup_ms`/`spindown_ms`.
```yaml
PlasmaSpindle:
  enable_pin: NO_PIN
  arc_ok_pin: NO_PIN          # Pin (input/event) — torch reports arc-established/arc-lost here
  arc_wait_ms: 0                # Integer 0-3000, default 0
  tool_num: 0
  speed_map: "0=0.000% 10000=100.000%"
  off_on_alarm: false
```

### 10.11 `NoSpindle:` — explicit no-spindle declaration
```yaml
NoSpindle:
```
Takes no fields (`group()` is inherited empty). Auto-created by default if no spindle section exists at all (confirmed in `MachineConfig::afterParse()`); only write it explicitly if you want to be unambiguous.

### 10.12 Multiple spindles / tool number ranges
- Each additional spindle needs entirely separate I/O pins from every other spindle (the config parser enforces global pin-uniqueness anyway, per §0.12).
- `tool_num:` on each spindle marks the **start** of that spindle's tool range; the range extends up to (but not including) the next spindle's `tool_num:`. Exactly one spindle must have `tool_num: 0` to make the full 0–255 range valid — confirmed by `MachineConfig::afterParse()`, which forces the first spindle to tool 0 (with a warning) if none was, and bumps any duplicate/out-of-order tool number up by 100 (also with a warning).
- Two spindles of the identical type (e.g. two `PWM:` blocks) are allowed and both function, but `$`-command introspection (e.g. `$pwm/output_pin`) will only ever report the first one's values — this is a firmware/tooling limitation, not a config error.

See §14 for the VFD/Modbus-driven spindle family (`Huanyang:`, `H2A:`, `YL620:`, `ModbusVFD:`, etc.), which is structurally different from everything above.

---

## 11. `macros:` section

```yaml
macros:
  startup_line0:                  # runs once, first time firmware reaches Idle
  startup_line1:
  macro0: G90&G53G0Z-1&G0X0Y0     # triggered by control.macro0_pin or $Macros/Run=0
  macro1:
  macro2:
  macro3:
  after_homing: g0 x1 y1           # (v3.7.6+) runs after a full $H homing sequence completes
  after_reset: g20                 # (v3.7.6+) runs after reset, only if system reaches Idle
  after_unlock: g91                # (v3.7.6+) runs after $X unlock
```

Macro line grammar rules:
- **A macro's entire body must fit on a single physical line** of the YAML file — this is a hard length constraint, not a style preference.
- Multiple GCode/`$` commands within one macro line are separated by `&` — this is a FluidNC-config-macro-only separator; it is **not** valid inside a `.nc` file run via `$SD/Run`, and it is not a general GCode feature.
- If a sequence is too long for one line, do not try to cram it in — instead write a separate `.nc`/text file (one command per line, no `&` separators) and have the one-line macro body be `$SD/Run=filename`.
- Realtime single-character commands (feed hold, overrides, etc.) are embedded in macro text as `#` followed by a documented 2-letter code (e.g. `#fr` for feed override reset), not as the raw non-ASCII byte.
- An empty macro key (e.g. `macro2:` with nothing after the colon) is valid and means "do nothing."

---

## 12. Full worked examples

### 12.1 Minimal 3-axis RMT-stepping machine with external stepstick drivers, no SD card, one PWM spindle

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
  reset_pin: NO_PIN
  feed_hold_pin: NO_PIN
  cycle_start_pin: NO_PIN
  safety_door_pin: NO_PIN
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

### 12.2 TMC2209 UART daisy config fragment (illustrates §5.4.6 + §9 interaction)

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
Note `uart1:` placed before `axes:` per §0.11 / §9, and each motor using a distinct `addr:` (0 and 1) on the same shared `uart_num: 1` bus.

---

## 13. `kinematics:` section

Ground truth: `Kinematics::group()` (`Kinematics/Kinematics.cpp`) delegates immediately to a `KinematicsFactory`, so `kinematics:` follows the same "one nested driver-type key selects the implementation" pattern as motors and spindles:

```yaml
kinematics:
  <KinematicsTypeName>:
    # type-specific fields, if any
```

If `kinematics:` is omitted entirely, `Kinematics::afterParse()` constructs a default `Cartesian` instance — so a plain 3-axis Cartesian machine needs no `kinematics:` section at all.

Registered type names (`KinematicsFactory::InstanceBuilder<...>` registrations):

### 13.1 `Cartesian:` (default; also the base class other kinematics build on)
```yaml
kinematics:
  Cartesian:
```
No config fields (`Cartesian` has no `group()` override).

### 13.2 `CoreXY:`
```yaml
kinematics:
  CoreXY:
```
No config fields (`group(){}` is an empty override) — CoreXY behavior is entirely driven by which axes/motors are wired, not by config values here. As noted in §5.3, CoreXY cannot combine two axes into one `homing.cycle` group.

### 13.3 `midtbot:` (note: lowercase registration name, unlike the others)
```yaml
kinematics:
  midtbot:
```
No config fields. A CoreXY variant with a fixed internal X-axis scaling factor (2.0), not exposed as a config item.

### 13.4 `parallel_delta:` (note: lowercase registration name)
```yaml
kinematics:
  parallel_delta:
    crank_mm: 70.0                        # Float 50.0-500.0, default 70.0
    base_triangle_mm: 179.437              # Float 20.0-500.0, default 179.437
    linkage_mm: 133.50                      # Float 20.0-500.0, default 133.50
    end_effector_triangle_mm: 86.603         # Float 20.0-500.0, default 86.603
    kinematic_segment_len_mm: 1.0             # Float 0.05-20.0, default 1.0
    use_servos: false                          # Boolean, default false
    up_degrees: -30.0                           # Float -90 to 0, default -30.0
```

### 13.5 `WallPlotter:`
```yaml
kinematics:
  WallPlotter:
    left_axis: 0             # Integer — which axis number drives the left cord (0=X, 1=Y, 2=Z...)
    left_anchor_x: -100        # Float, default -100
    left_anchor_y: 100          # Float, default 100
    right_axis: 1                 # Integer, default 1
    right_anchor_x: 100             # Float, default 100
    right_anchor_y: 100               # Float, default 100
    segment_length: 10                  # Float, default 10 — max length of line segments used to approximate curved motion
```

---

## 14. VFD/Modbus spindle family

These are structurally distinct from §10's spindles: ground truth is `Spindles::VFDSpindle::group()` (`Spindles/VFDSpindle.cpp`) plus each specific protocol's own `group()`.

### 14.1 Fields common to every VFD spindle type
```yaml
<VFDTypeName>:
  uart_num: 1                 # Integer — references a top-level uartN: section, see §9. Preferred/current form.
  # -- OR, older/generator-only form (do not mix): a nested uart: sub-block directly under the spindle --
  modbus_id: 1                 # Integer 0-247, default 1 — the RS485/Modbus slave address of the VFD
  debug: 2                       # Integer 0-5, default 2
  poll_ms: 250                     # Integer 250-20000, default 250
  retries: 5                         # Integer, default 5
  # plus all of Spindle::group()'s common fields from §10.2 (tool_num, speed_map, off_on_alarm, atc, m6_macro, s0_with_disable, disable_with_s0, spinup_ms, spindown_ms)
```
`uart_num:` must reference a top-level `uartN:` section defined earlier in the file (same rule as §9/§5.4.6). The baud rate and mode for VFD communication are configured on that `uartN:` section, not here.

### 14.2 Protocol-specific type names with no extra fields beyond §14.1
These select a fixed, built-in command/register protocol for a specific well-known VFD brand/model and add nothing to the field list:
```yaml
Huanyang:
H2A:
YL620:
DeltaMS300:
FolinnBD600:
H100:
MollomG70:
NowForever:
SiemensV20:
DanfossVLT2800:
```
Example:
```yaml
Huanyang:
  uart_num: 1
  modbus_id: 1
  tool_num: 0
  speed_map: "0=0% 24000=100%"
```

### 14.3 `ModbusVFD:` — generic/user-defined Modbus VFD with explicit command strings
Adds a distinct set of fields on top of §14.1 for VFDs with no built-in protocol support:
```yaml
ModbusVFD:
  uart_num: 1
  modbus_id: 1
  model: "My VFD"           # String — descriptive only
  min_RPM: 0                  # Integer
  max_RPM: 24000                # Integer
  cw_cmd: ""                      # String — raw Modbus command token for clockwise-run
  ccw_cmd: ""                       # String — counterclockwise-run
  off_cmd: ""                         # String — stop
  set_rpm_cmd: ""                       # String — set-speed command
  get_min_rpm_cmd: ""                     # String, optional
  get_max_rpm_cmd: ""                       # String, optional
  get_rpm_cmd: ""                             # String, optional — if omitted, delay-based timing (spinup_ms/spindown_ms) is used instead of polling actual RPM
```
The exact command-token string grammar (register addresses/formats) is VFD-protocol-specific and not simply inferable — treat `cw_cmd`/`ccw_cmd`/etc. values as opaque strings whose correct content must come from the specific VFD's Modbus register documentation, not from this spec.

---

## 15. `user_outputs:` and `user_inputs:` sections

General-purpose spare I/O exposed to GCode M-codes / `$` commands, independent of any axis/spindle/coolant function. Ground truth: `Machine::UserOutputs::group()` and `Machine::UserInputs::group()`.

```yaml
user_outputs:
  analog0_pin: NO_PIN            # Pin (output)
  analog1_pin: NO_PIN
  analog2_pin: NO_PIN
  analog3_pin: NO_PIN
  analog0_hz: 5000                 # Integer 1-20000000, default 5000
  analog1_hz: 5000
  analog2_hz: 5000
  analog3_hz: 5000
  digital0_pin: NO_PIN               # Pin (output)
  digital1_pin: NO_PIN
  digital2_pin: NO_PIN
  digital3_pin: NO_PIN
  digital4_pin: NO_PIN
  digital5_pin: NO_PIN
  digital6_pin: NO_PIN
  digital7_pin: NO_PIN

user_inputs:
  digital0_pin: NO_PIN               # Pin (input) — indices 0-7, same naming as user_outputs
  digital1_pin: NO_PIN
  digital2_pin: NO_PIN
  digital3_pin: NO_PIN
  digital4_pin: NO_PIN
  digital5_pin: NO_PIN
  digital6_pin: NO_PIN
  digital7_pin: NO_PIN
  analog0_pin: NO_PIN                 # Pin (input) — indices 0-3
  analog1_pin: NO_PIN
  analog2_pin: NO_PIN
  analog3_pin: NO_PIN
```
Exactly 4 analog and 8 digital slots on each side — this is a fixed array size (`MaxUserAnalogPin`/`MaxUserDigitalPin`) in the firmware, not extensible via config.

---

## 16. `start:` and `parking:` sections

### 16.1 `start:` — boot/reset behavior
Ground truth: `Machine::Start::group()` (`Machine/MachineConfig.h`).
```yaml
start:
  must_home: true                  # Boolean, default true — refuses motion commands until $H has been run
  deactivate_parking: false          # Boolean, default false
  check_limits: true                   # Boolean, default true — Alarm (rather than Idle) at boot if a limit switch already reads active
```

### 16.2 `parking:` — safety-door parking-motion behavior
Ground truth: `Parking::group()` (`Parking.cpp`).
```yaml
parking:
  enable: false                         # Boolean, default false
  axis: z                                 # Axis letter, default z
  target_mpos_mm: -5.0                      # Float, default -5.0 — machine position to retract to when parking
  rate_mm_per_min: 800.0                      # Float, default 800.0 — fast parking-retract feed rate
  pullout_distance_mm: 5.0                      # Float 0-3e38, default 5.0 — initial slow pull-out distance before the fast retract
  pullout_rate_mm_per_min: 250.0                  # Float, default 250.0 — feed rate for the initial pull-out move
```
`enable_parking_override_control` (§1, top-level scalar) is a separate switch that additionally allows GCode `M56` to toggle parking on/off at runtime; `parking.enable` is the base on/off switch for the feature existing at all.

---

## 17. ATC (automatic tool change) sections

Ground truth: `ToolChangers/atc.h` (base, empty `group()`) and `ToolChangers/atc_manual.h` (the only currently-implemented concrete type, `atc_manual`). A spindle's `atc:` field (§10.2) is a String naming the type key used here, following the same nested-driver-type pattern as kinematics/motors/spindles — the ATC block itself is registered via `ATCFactory` at the top level, not nested inside the spindle.

```yaml
atc_manual:
  safe_z_mpos_mm: 50.0                       # Float -100000 to 100000, default 50.0
  probe_seek_rate_mm_per_min: 200.0            # Float 1-10000, default 200.0
  probe_feed_rate_mm_per_min: 80.0               # Float 1-10000, default 80.0
  change_mpos_mm: 0.0 0.0 0.0 0.0 0.0 0.0          # Float Array, one value per axis — manual tool-change waypoint
  ets_mpos_mm: 0.0 0.0 0.0 0.0 0.0 0.0                # Float Array — electronic tool-setter probe position
  ets_rapid_z_mpos_mm: 0.0                             # Float, default 0
```

**Float Array grammar (corrects an earlier, wrong example that used bracket/comma syntax like `[0.0, 0.0, ...]` — that is not valid).** Ground truth: `Parser::floatArray()` (`Configuration/Parser.cpp`):
```
float_array := float (" "+ float)*
```
Just plain whitespace-separated floats, parsed with the same `from_float` used everywhere else in the config — **no brackets, no commas, no other delimiter**. The number of values you provide should match the number of defined axes (one float per axis, in axis-letter order), since that's how `change_mpos_mm`/`ets_mpos_mm` are consumed at runtime, though the parser itself doesn't enforce a specific count — it will happily accept too few or too many floats and let the consuming code deal with (or mishandle) the mismatch, so get the count right rather than relying on an error to catch it. If the value is empty or a float fails to parse, the whole array is discarded and FluidNC logs `"Using default value"`, falling back to an empty array rather than erroring the file load — a config with the wrong syntax here won't fail to load, it will just silently behave as if `change_mpos_mm`/`ets_mpos_mm` were never set.

---

## 18. `i2c1:`, `i2c2:`, ... top-level sections

Ground truth: `I2CBus::group()` (`Machine/I2CBus.cpp`). Simple section, only needed if `extenders:` (§19 — itself provisional/do-not-use) or some other I2C peripheral is in use.
```yaml
i2c1:
  sda_pin: NO_PIN         # Pin
  scl_pin: NO_PIN         # Pin
  frequency: 100000         # Integer, default 100000 (100kHz standard I2C)
```
Recall from §1 that these sections are gated in `MachineConfig::group()` behind the same build flag as `i2so:`, which reads as a probable copy-paste artifact in the firmware rather than an intentional coupling — worth being aware of if `i2cN:` is ever unavailable on a build that otherwise has no I2S output hardware. (Per the earlier conversation, this is being corrected at the source, so it may no longer apply to newer FluidNC builds.)

---

## 19. `extenders:` section (I2C GPIO expander chips) — **DO NOT USE (provisional, may be removed)**

**This whole feature is provisional and may be removed from FluidNC in a future version.** Do not generate configs using `extenders:`/`pinextenderN:`/`pinextN.M` pins. Documented here for recognition purposes only, in case it's encountered in an existing config someone hands to a generator for editing.

Ground truth: `Extenders::Extenders::group()` (`Extenders/Extenders.cpp`) and `I2CPinExtenderBase::group()`.

```yaml
extenders:
  pinextender0:                  # up to pinextender9: — 10 slots
    <ExtenderTypeName>:
      busId: 0                    # Integer — which top-level i2cN: bus this expander is on
      interrupt0: NO_PIN            # Pin (input), optional — hardware interrupt line, one per group of 16 IO pins
      interrupt1: NO_PIN
      interrupt2: NO_PIN
      interrupt3: NO_PIN
```
Registered `<ExtenderTypeName>` values: `pca9539`, `pca9535_9555`. Neither adds fields beyond the `I2CPinExtenderBase` set shown above. This feature requires an `i2cN:` bus section (out of scope of this document — not yet sourced). **Provisional status aside, prefer `user_outputs:`/`user_inputs:` (§15) for any spare-I/O need in a generated config.**

---

## 20. Remaining known gaps (still not sourced from code)

- Full board-specific pin legality tables, across both supported MCU families (ESP32 primary target, RP2040 development build) — this remains **intentionally** out of scope per the original stated design goal (§0's opening note), not an oversight; it's the one item deliberately deferred to the planned JSON Schema elaboration rather than something left unverified.
- As of this pass, every top-level section reachable from `MachineConfig::group()` is enumerated (§1) and cross-checked against source, and both previously-open tokenizer grammars — `Parser::floatArray()` (§17) and `Parser::speedEntryValue()` (§10.1) — are now fully documented from source. No other known gaps remain.

## 21. `oled:` section — optional onboard status display

Ground truth: `OLED.h`/`OLED.cpp`. Registered as a `ConfigurableModule` named `"oled"`, mounted directly at the top level via `ConfigurableModuleFactory::factory(handler)` inside `MachineConfig::group()` — so `oled:` is a plain top-level key, the same pattern as a spindle type name, not nested under any wrapper section. Entirely optional; omit it if there's no display attached.

```yaml
oled:
  report_interval_ms: 500      # Integer 100-5000, default 500
  i2c_num: 0                     # Integer, default 0 — which top-level i2cN: bus the display is on
  i2c_address: 60               # Integer, default 60 (0x3c) — the display's I2C address; write it in DECIMAL, see §2
  width: 64                          # Integer, default 64 — display width in pixels
  height: 48                           # Integer, default 48 — display height in pixels
  flip: true                             # Boolean, default true
  mirror: false                            # Boolean, default false
  radio_delay_ms: 0                          # Integer, default 0
```
`i2c_address`'s in-source default is written as the hex literal `0x3c` in the C++ initializer, but the config-file *value* itself must be written in plain decimal (`60`) — see the confirmed hex-rejection rule in §2's Integer row.

---

## 22. `rgbled:` section — status-indicator RGB LED (e.g. NeoPixel/WS2812) — **DO NOT USE (provisional, may be removed)**

**The entire `Listeners`/`SysListener` framework that `rgbled:` belongs to is likely to be removed from FluidNC.** Do not generate configs using `rgbled:`. Documented here for recognition purposes only, same treatment as `extenders:`/`pinextN.M` in §19 — in case it's encountered in an existing config someone hands to a generator for editing, not because it should be generated.

Ground truth: `Listeners/RGBLed.h`/`RGBLed.cpp`, registered as `SysListenerFactory::InstanceBuilder<RGBLed>("rgbled")`, mounted directly at the top level via `Listeners::SysListenerFactory::factory(handler)` inside `MachineConfig::group()` — a plain top-level key, same mounting pattern as `oled:` and every spindle type. This was found only by re-reading `MachineConfig::group()` in full; it had been missed entirely in earlier passes of this document.

```yaml
rgbled:
  pin: NO_PIN            # Pin (output) — data pin to the LED/LED strip
  index: 0                 # Integer, default 0 — which LED in a chain this config addresses
  idle: "007F00"              # String, 6 hex digits RRGGBB (no leading #), default shown
  alarm: "7F0000"
  checkMode: "b936bf"
  homing: "501f00"
  cycle: "7f4422"
  hold: "777744"
  jog: "007f3f"
  safetyDoor: "3f7f00"
  sleep: "001F00"
  configAlarm: "7f0000"
```

**Two gotchas worth flagging explicitly:**
- **These 10 color fields are the one place in the entire config surface that uses camelCase key names** (`checkMode`, `safetyDoor`, `configAlarm`) instead of the `snake_case` convention used everywhere else in FluidNC config (`step_pin`, `run_amps`, `off_on_alarm`, etc.). Ground truth: `handler.item(name, str)` is called with the literal camelCase strings `"checkMode"`, `"safetyDoor"`, `"configAlarm"` in `RGBLed.h` — this is not a typo in this document, it's a real inconsistency in the firmware itself. Writing `check_mode:`/`safety_door:`/`config_alarm:` here will silently fail to match (§0.7 — unrecognized key, ignored, not fatal) rather than erroring, so get the casing right.
- **Color values are hex strings without a leading `#`** — write `"7F0000"`, not `"#7F0000"`. The special string `"none"` means "leave this color unchanged from whatever it currently is" (`parseColor()` returns `-1` for `"none"`, which the runtime treats as a no-op rather than a color), not "off"/black — for actual off/black, use `"000000"`.

---

## 23. Recommended next step

Convert §§1–19 into a JSON Schema with `oneOf` branches for the motor/spindle/kinematics driver-type unions (§5.4, §10, §13), keeping the pin-string grammar (§3) as a regex pattern rather than an enum, per the original phased plan. Before that pass, it would be worth re-deriving §5 (axes/motors) from `Machine/Axis.cpp`/`Machine/Motor.cpp`/`Motors/*.cpp` the same way §10/§13/§14/§16/§17 were derived here, since that section is still wiki-sourced and the wiki has already been shown to diverge from source in at least the spindle section.
