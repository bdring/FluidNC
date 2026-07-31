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
// @default 255 -- special "never auto-disable" value (Grbl compatibility)
// Milliseconds of inactivity before motors are automatically disabled.
// Any value other than 255 (0-254 or 256+) is a real millisecond delay;
// motors can also be disabled manually at any time with $MD.
handler.item("idle_ms", _idleMsecs, 0, 10000000);
```

```c++
// @config engine
// @default board-dependent (DEFAULT_STEPPING_ENGINE, applied in afterParse())
// Method used to generate step pulses in firmware...
handler.item("engine", _engine);
```

Rules:

- The `@config <name>` line opens the block.
- The very next line **must** be `@default <text>`, one line, no
  continuation -- a generator should treat a missing `@default` as an error,
  the same as a name mismatch. Keep it short (the value, plus a terse
  qualifier if the bare value would be misleading on its own, as in the two
  examples above); a fuller explanation of *why* belongs in the description
  below, not crammed into the `@default` line itself.
- An optional `@unit <text>` line may follow `@default`, before the
  description, when the field name's own suffix (`_ms`, `_us`, `_mm`,
  `_amps`, ...) doesn't already say it plainly enough to be worth restating
  -- most fields don't need this.
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
  updating the comment.
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
