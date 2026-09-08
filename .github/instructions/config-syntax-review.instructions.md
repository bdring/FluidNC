---
applyTo: "FluidNC/src/Configuration/**,FluidNC/src/Machine/**,FluidNC/src/Motors/**,FluidNC/src/Spindles/**,FluidNC/src/Kinematics/**,FluidNC/src/ToolChangers/**,FluidNC/src/Extenders/**,FluidNC/src/Pin.*,FluidNC/src/Pins/**,FluidNC/src/Uart*,FluidNC/src/OLED.*,FluidNC/src/Listeners/**,FluidNC/src/Probe.*,FluidNC/src/Control.*,FluidNC/src/CoolantControl.*,FluidNC/src/Parking.*,FluidNC/src/Stepping.*"
---

# Config File Syntax Impact Review

## Why this file exists

`FluidNC/docs/config_items.yaml` is generated from this source code by
`tools/build_config_docs.py`, reading the `// @config` annotations next to each
`handler.item()`/`handler.section()` call (see `FluidNC/src/Configuration/ItemDocs.md`). It is
the single source of truth for `config.yaml`: the validator schema is built from it at runtime
by `tools/config_schema_adapter.py` (used by `tools/validate_fluidnc_config.py` and
`tools/fluidnc_config_mcp_server.py`), and structural metadata it needs beyond per-item ranges
(`section_meta`, `pin_namespaces`, `vfd_named_types`) is emitted by the same generator.

So a `handler.item()`/`handler.section()`/`InstanceBuilder` change that lacks a matching
`// @config` annotation update **fails or skews the generated `config_items.yaml`** — the
generator errors on an un-annotated `handler.item()` call, and a stale `@default`/`@config`
block drifts the docs. `tools/fluidnc-config-spec.md` (the prose reference) is still
hand-maintained and can silently go stale.

Your job when reviewing a PR that touches these files: determine whether the change actually
affects `config.yaml` syntax as an external user or LLM would experience it, and if so, say so
explicitly and ask the author to update the docs. Most changes in these directories DO affect
config syntax, since that's what most of this code exists to define — so the default assumption
should be "this probably needs a doc check", not the other way around.

## Signatures that mean the change affects config.yaml syntax

Look for diffs touching any of the following. Each is a load-bearing part of the config file
format, not an implementation detail:

- **Any `handler.item("key_name", ...)` call** — added, removed, or with the key string, value
  type (bool/int/float/string/enum/Pin/Macro/speed-map/float-array/UartData), numeric min/max
  bounds, or default value changed. This is the single most common source of drift: a changed
  min/max or default here means the schema's corresponding range/default is now wrong, and the
  spec's documented value is now wrong, even if the key name itself didn't change.
- **Any `handler.section("name", ...)` or `handler.sections("name", first, limit, omit0, ...)`
  call** — added, removed, or renamed. This defines section names (including numbered ones like
  `uart1:`, `i2c0:` — check whether `omit0`/`first`/`limit` changed, since that changes which
  numbers are valid or whether an un-numbered form is allowed).
- **Any `InstanceBuilder<T> registration("name")` under a `GenericFactory`** (`MotorFactory`,
  `SpindleFactory`, `KinematicsFactory`, `ATCFactory`, `UartFactory`, `ConfigurableModuleFactory`,
  `SysListenerFactory`, extender factories, etc.) — added, removed, or renamed. This is how a
  type-selector key like `tmc_2209:`, `PWM:`, `CoreXY:`, or `pca9539:` comes to exist at all.
- **Changes to `Pin::parse()` or anything under `Pins/`** — this defines the pin string grammar
  itself (`gpio.N`, `i2so.N`, `NO_PIN`, `void`, `uart_channelN.M`, the deprecated `pinextN.M`,
  and the `:high`/`:low`/`:pu`/`:pd`/`:dsN` attribute syntax). A change here can invalidate the
  schema's pin regex pattern even if no single config *field* changed.
- **Changes to `Configuration/Parser.cpp` or `Configuration/Tokenizer.cpp`** — these define
  file-format-level rules that apply across the whole file: indentation handling, whitespace
  trimming around `:`, comment syntax, case-insensitive key/value matching (`strncasecmp`,
  `equal_ignore_case`), how booleans/integers/floats are parsed, and the Speed Map / float-array
  tokenizer grammars. A change here can invalidate spec §0 (file-level rules) even if it touches
  no specific config field at all.
- **Changes to an `EnumItem` array** (e.g. `stepTypes`, `trinamicModes`, `messageLevels2`) — this
  changes which string values are valid for an enum-typed field.
- **Changes to a config-visible member variable's default value or declared range**, even if the
  `handler.item()` call itself didn't move — e.g. `float _maxRate = 1000.0f;` changing, or a
  `constrain_with_message` bound changing elsewhere.

## What to do when you spot one of these

1. Say explicitly which change you believe affects config.yaml syntax, and why (cite the
   specific `handler.item`/`handler.section`/`InstanceBuilder`/etc. line).
2. Ask the author to check and, if needed, update:
   - the `// @config` annotation block next to the changed `handler.item()` call (name,
     `@default`, description) — see `FluidNC/src/Configuration/ItemDocs.md`. Then regenerate:
     `python3 tools/build_config_docs.py`, and eyeball the `FluidNC/docs/config_items.yaml` diff.
   - a new `handler.section()`/`InstanceBuilder` type needs a `SECTIONS` entry in
     `tools/build_config_docs.py`; a new pin namespace needs a `// @pin_namespace` annotation
     in the relevant `src/Pins/*PinDetail.cpp`.
   - `tools/fluidnc-config-spec.md` — the prose description of this field/section (hand-maintained).
   - `tools/fluidnc_validate_core.py` — its `ENUM_FIELDS` table, only if an enum field's set of
     valid values changed (permissive-mode casing normalization list).
3. Suggest running `tools/validate_fluidnc_config.py` against real example configs (this repo's
   `example_configs/`, or the sibling `bdring/fluidnc-config-files` repo) if the change could
   plausibly break existing configs — e.g. a narrowed range, a renamed key, or a removed type.
4. If the PR already updates the docs/schema to match, say so and move on — don't ask for
   changes that are already present.

Suggested comment template:

> ⚠️ **Config syntax impact**: this change to `<file>:<line>` <adds/removes/changes> `<key or
> section or type name>` <in a way that changes its type/range/default/valid values>. Please
> confirm the `// @config` annotation is updated and `config_items.yaml` regenerated (and
> `tools/fluidnc-config-spec.md` / `tools/fluidnc_validate_core.py`'s `ENUM_FIELDS` if
> relevant), or confirm no config-visible behavior actually changed.

## What NOT to flag (avoid noise)

- Pure refactors that don't change any `handler.item`/`handler.section`/`InstanceBuilder` call's
  arguments — renaming a private member variable, reordering unrelated code, adding comments,
  clang-format-only diffs.
- Changes confined to ISR-context motion code, GCode.cpp, WebUI, or other subsystems unrelated to
  config file parsing, even if they happen to live in a directory this file's `applyTo` matches
  incidentally (rare, but e.g. a stray helper function).
- Changes confined to `test/` or `fixture_tests/`.
- Internal validation logic changes that don't change what a valid `config.yaml` looks like
  (e.g. improving an error message's wording without changing what triggers it).
