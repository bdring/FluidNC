# Documenting config items in source

The wiki (wiki.fluidnc.com) is meant to describe every `config.yaml` item,
but it drifts from the firmware and uses more than one inconsistent format.
This convention lets the description, default-value nuance, and unit for a
config item live next to the `handler.item(...)` call that actually defines
it, so a script can regenerate the wiki-facing docs (and a machine-readable
summary for the config wizard's tooltips) straight from source instead of
by hand.

Everything a script *cannot* already infer from the C++ itself needs an
annotation. Everything it *can* infer should not be repeated in prose:

- **Type and range** come from the `item()` overload used (`HandlerBase.h`)
  and its `minValue`/`maxValue` arguments -- don't restate these in text.

The one exception is the default value, and it's a deliberate exception: it
is **always** stated explicitly via `@default`, even when a script could, in
principle, read it straight off the member initializer. Two reasons:

1. Some real defaults are *not* a literal a script can find at all -- they're
   established by code. `Stepping::_engine`'s real default isn't its `nullptr`
   initializer, it's whatever `DEFAULT_STEPPING_ENGINE` resolves to on the
   target board, applied in `afterParse()`. `TMC2209Driver::_homing_current`
   is initialized to `0`, but `afterParse()` then substitutes `_run_current`
   whenever it was left at that `0` -- the field's *real* default is "same as
   run_amps," not the literal `0` sitting in the header. A script has no way
   to discover either of these by reading the initializer alone.
2. Even when the literal *is* the whole story, restating it explicitly next
   to the description is what makes the annotation self-contained -- a reader
   (or the generator) shouldn't have to go cross-reference the class's field
   declarations just to know what a plain, ordinary default is.

## The `@config` comment block

Place it immediately above the `handler.item(...)` call it documents, inside
the class's `group()` method -- no blank line in between. The name after
`@config` must exactly match the string literal passed as the item's `name`
argument; a generator should treat a mismatch as an error (it means the
comment and the code have drifted apart), not silently ignore it.

```c++
// @config idle_ms
// @default 255
// @default_note special "never auto-disable" value (Grbl compatibility)
// Milliseconds of inactivity before motors are automatically disabled.
// Any value other than 255 (0-254 or 256+) is a real millisecond delay;
// motors can also be disabled manually at any time with $MD.
handler.item("idle_ms", _idleMsecs, 0, 10000000);
```

```c++
// @config engine
// @default (none)
// @default_note board-dependent (DEFAULT_STEPPING_ENGINE, applied in afterParse())
// Method used to generate step pulses in firmware...
handler.item("engine", _engine);
```

Rules:

- The `@config <name>` line opens the block.
- The very next line **must** be `@default <literal>`, one line, no
  continuation -- a generator should treat a missing `@default` as an error,
  the same as a name mismatch. `<literal>` must be a single bare value in the
  field's own type (a number, `true`/`false`, a bare or quoted string,
  `NO_PIN`, a hex constant, ...) with nothing else on the line -- no
  qualifier, no parenthetical, no explanation. When the real default
  genuinely isn't a fixed literal at all (board-dependent, substituted by
  other code in `afterParse()`, ...), write the reserved token `(none)`
  instead of inventing one. This keeps `@default` mechanically parseable in
  every case: a downstream consumer (e.g. the config wizard's silent-pre-fill
  logic) can trust it's either a real, directly usable value or the explicit
  "no such value exists" sentinel, never free text it has to guess how to
  parse.
- An optional `@default_note <text>` line may immediately follow `@default`
  -- one line, no continuation. This is where the qualifier that used to be
  crammed onto the `@default` line itself now goes: why the literal might be
  misleading on its own (idle_ms's example above), or, when `@default` is
  `(none)`, what actually determines the real default (engine's example
  above). A fuller explanation of *why* still belongs in the description
  below, not here -- keep this short.
- An optional `@tuning <typical|per-machine>` line may follow
  `@default`/`@default_note`, before `@unit`/the description. It answers a
  narrower question than
  "does a compiled default exist" (true of nearly every item): is that
  default likely *correct, or at least safe/harmless, for most machines* --
  `typical` -- or is it a placeholder/starting point that needs real
  per-machine data before the config will actually work right --
  `per-machine`. A physical dimension, a bus/device address, a current
  rating, or anything else that varies with the specific hardware attached
  is `per-machine` even when the initializer is a plausible-looking number
  (e.g. Parallel Delta's `crank_mm`, or a TMC driver's `r_sense_ohms`). A
  behavior toggle, timing constant, or cosmetic setting whose compiled
  default is safe to leave alone on nearly any machine is `typical`. Omit
  the line when genuinely unclassified rather than guessing -- a missing
  `@tuning` is not an error, unlike a missing `@default`. This exists so a
  config wizard's "pre-fill vs. force the user to look at it" logic can be
  generated from source instead of hand-maintained (see
  FluidNC-config-wizard's `TUNING_CLASSIFICATION_REVIEW.md` for the review
  that produced the first pass of these).
- An optional `@pin_attributes <value>` line, valid only on a `type: pin`
  item, may follow `@default`/`@default_note`/`@tuning`, before `@unit`/the
  description. A single bare value from a fixed vocabulary -- never
  composed (e.g. never `output spi`) -- describing what a physical board
  pin needs to be eligible for this field:
  - `input` / `output` / `io` / `pwm` / `isr` -- an ordinary GPIO-style
    field, matched against a board's own generic-pin role selector.
    `pwm`/`isr` are for a field that needs that specific hardware
    capability, not just a plain direction. `io`/`adc`/`dac` are reserved
    -- valid values, but no current field needs them. `io` in particular
    is for a genuinely bidirectional field with no dedicated hardware bus
    block behind it -- e.g. a future bit-banged I2C driver's sda/scl,
    where the pins are plain GPIOs toggled directly in software rather
    than handed to the native I2C peripheral. Not needed for `i2cN.sda_pin`
    /`scl_pin` today, even though I2C is electrically bidirectional too --
    see `i2c` below, which already covers that case more specifically for
    the one real (hardware-block) I2C driver that exists now.
  - `spi` / `i2s` / `uart` / `i2c` -- this field IS one of that bus's own
    native signal lines (e.g. `spi.mosi_pin`, `uartN.txd_pin`), resolved
    against the board's fixed peripheral wiring, not the generic-pin role
    selector -- so no separate direction value is needed alongside it (a
    board's own `spi:` block, for instance, already establishes mosi as an
    output; the field doesn't need to repeat that).

  This is deliberately explicit rather than derived from the nearest
  `Pin::setAttr(Pin::Attr::...)` call in source, even though one usually
  exists: some classes call `setAttr(Pin::Attr::Input)` again in `deinit()`
  to release/tri-state the pin on teardown (e.g. every `OnOffSpindle.h`
  subclass's `deinit()`), which would make a naive scrape see two
  conflicting calls per field and have no principled way to prefer the
  "real" one. A human-authored value that a reviewer can check against the
  nearest non-`deinit()` `setAttr()` call is simpler and more reliable than
  teaching a script to know which call sites to ignore.
- An optional `@unit <text>` line may follow `@default`/`@default_note`/
  `@tuning`/`@pin_attributes`, before the description, when the field
  name's own suffix (`_ms`, `_us`, `_mm`, `_amps`, ...) doesn't already say
  it plainly enough to be worth restating -- most fields don't need this.
- Every plain `//` comment line after that (no intervening code, no blank
  line) is part of the description, in order. A blank comment line (`//`
  alone) is a paragraph break; anything else joins with a single space.
- One `@config` block documents exactly one `item()` call. Shared base-class
  fields (e.g. `step_pin`/`direction_pin`/`disable_pin` on `StandardStepper`)
  are documented once, where `item()` is actually called for them -- derived
  classes that inherit the same `group()` call don't repeat the annotation.
- The generator cross-checks `@default` against whatever literal it can find
  in the field's own initializer, where one exists, and warns (doesn't fail)
  on an apparent mismatch -- that's a strong signal the annotation drifted
  from a later code change, e.g. someone changed the initializer without
  updating the comment. Skipped entirely when `@default` is `(none)` -- there's
  no literal to compare against by definition.
- Every `handler.item(...)` call gets a `@config`/`@default` block, full
  stop -- the generator treats a call with no matching block as an error.
  This is a change from an earlier version of this convention, which allowed
  skipping the block entirely when there was "nothing to say beyond the
  signature": now there's always something to say, because `@default` must
  be stated regardless. If a field genuinely needs no description beyond its
  default, the block can be just the two required lines with no prose below:

```c++
// @config pulse_us
// @default 4
handler.item("pulse_us", _pulseUsecs, 0, 30);
```

## Overriding an inherited item's effective default: `@default_for`

Some items are declared once in a shared base class's `group()` (one
`@config`/`@default`/`handler.item()` site, documented once, same rule as
above) but a concrete subclass establishes a *different* real default for
it at a separate point in code -- typically `init()`, not `group()` --
via a call the generator has no way to connect back to the item by name.
`Spindle::speed_map` is the motivating case: `handler.item("speed_map",
_speeds)` lives once in `Spindle::group()` with default `""` (empty --
genuinely correct when nothing else applies), but every concrete spindle
type's `init()` does `if (_speeds.size() == 0) { linearSpeeds(...); }` (or
`shelfSpeeds(...)`) to install its own real working curve when the config
left it unset -- and the two arguments differ by type (PWM's is not
Laser's is not BESC's).

For this shape, place a `@default_for <name>` block *inside that
subclass's own* `group()`/`groupCommon()` body (anywhere in it -- it isn't
tied to a specific `handler.item()` call the way `@config` is, since the
call that actually establishes the value lives elsewhere). Same two lines
as `@config`'s opening -- `@default <literal>` required, `@default_note
<text>` optional -- but nothing else: no `@tuning`/`@unit`/description
follows, and it does **not** need a matching `handler.item()` call in this
class's own body (unlike `@config`, an orphan `@default_for` -- one whose
name never appears in the section's fully-merged item set -- is a
warning, not an error, since the generator can't always see far enough
ahead in the merge to be sure).

```c++
// In PWMSpindle.h's PWM::group():
// @default_for speed_map
// @default 0=0% 10000=100%
// @default_note applied by PWM::init() only when speed_map is left unset
OnOff::group(handler);
```

The generator applies `@default_for` overrides as a final pass after
merging every contributor for a section (see `tools/build_config_docs.py`'s
`merge_section()`) -- so a subclass's own override always wins over
whatever the shared base class declared, regardless of contributor order,
and a class with nothing to override (e.g. `Relay`, which reuses `OnOff`'s
default unchanged) simply doesn't need one.

## Overriding an inherited item's `@pin_attributes`: `@pin_attributes_for`

Same shape and purpose as `@default_for` above, one level over: a shared
base class's `output_pin`-style field has one real `@pin_attributes` value
for most concrete subclasses but a genuinely different one for others,
established by each subclass's own `init()`. `OnOffSpindle.h`'s
`output_pin` is the motivating case: plain `output` (a digital on/off
signal) for `OnOff`/`Relay`/`DAC`, but `pwm` for `PWM`/`Laser`/`10V`/`BESC`
-- each of those calls its own `_output_pin.setAttr(Pin::Attr::PWM, ...)`
in its own `init()`, never reusing `OnOff::init()`'s plain
`Pin::Attr::Output` for that field.

```c++
// In PWMSpindle.h's PWM::group():
// @pin_attributes_for output_pin
// @pin_attributes pwm
OnOff::group(handler);
```

Same override-as-final-pass timing as `@default_for` -- a subclass's own
`@pin_attributes_for` always wins over whatever the shared base class's
own `@pin_attributes` declared. A class that reuses the base value
unchanged (e.g. `Relay`, `DAC`) simply doesn't need one.

## Data-driven item lists (no per-item `handler.item()` call to annotate)

A few classes don't call `handler.item(name, member)` once per field at all.
`Control::group()` (`Control.cpp`) instead loops over a runtime-built
`std::vector<ControlPin*>` and calls `handler.item(pin->legend(), *pin)`
once per element -- there is exactly one `item()` call in the source
text, executed N times, and its `name` argument is a function call
(`pin->legend()`), not a string literal. The `@config` convention (and the
generator's parser, which requires a literal name) doesn't apply to that
call directly -- there's nothing next to it that names a single field.

For this shape, annotate where each item is actually *registered* instead --
here, each `_pins.push_back(new ControlPin(&event, "name", letter))` call in
the constructor, since that's the one place in source where a specific
item's name literal and purpose are both present:

```c++
// @config safety_door_pin
// @default NO_PIN
// Stops motion and enters Door mode when active (typically wired to an
// enclosure door switch). Resume with cycle_start once the door is closed.
_pins.push_back(new ControlPin(&safetyDoorEvent, "safety_door_pin", 'D'));
```

This is a documented exception that `tools/gen_config_docs.py` (the per-class
parser) still can't see -- it only understands the literal
`handler.item("name", ...)` shape. `tools/build_config_docs.py` (the
whole-tree aggregator) does pick these up, though, via a separate code path
(`LIST_MODE_SECTIONS`/`list_mode_section()`) that scans a whole file directly
for `@config` blocks rather than requiring a matching `handler.item()` call --
used for `Control.cpp`, `Machine/UserInputs.cpp`, `Machine/Macros.cpp`, and
`Listeners/RGBLed.h`. Making `gen_config_docs.py` itself understand this
shape (so a single-file/single-class run picks it up too, not just the
whole-tree build) is still future work, tracked wherever this convention's
rollout status is tracked (see PROGRESS.md).

## What this feeds

A generator script (see `tools/gen_config_docs.py` at the repo root) walks a
`group()` method, pairs each `handler.item(...)` call with its `@config`
block (if any) and the field's real default, and emits a machine-readable
per-section summary (name, type, range, default, description) intended to:

1. Replace the hand/LLM-maintained `fluidnc-config-schema.json` and
   `fluidnc-config-spec.md` as generated output instead of manually kept
   files.
2. Feed the FluidNC config wizard's hoverable tooltips.
3. Eventually let CI flag wiki pages that have drifted from source.

This is being rolled out module by module, starting with `Stepping` as a
pilot, not applied across the whole tree yet.
