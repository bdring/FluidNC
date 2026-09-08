# Plan: retire `fluidnc-config-schema.json`, validate against `config_items.yaml` via an adapter

Status: WI1 + WI2 implemented. WI3 follows approach **B** (below).

**WI3 approach decision (post-design):** the plan originally had the adapter
re-emit all 62 `$defs` of the retired hand-schema byte-for-byte so
`normalize_permissive()` in `fluidnc_validate_core.py` would need no changes
(old step 9). On implementation it became clear `normalize_permissive()`
reaches into the hand-schema's internal shapes (`motorBlock.properties`
splitting `$ref` vs inline, `extendersSection.additionalProperties.properties`,
`kinematicsSection.properties`, `axisLetter`, ~14 flat `…Section` defs) in a
way that's fragile to reproduce. **Approach B:** point `normalize_permissive()`
at `config_items.yaml` directly (it already has the clean
`section → {field: {...}}` structure plus `section_meta` / `spindle_sections`
/ `vfd_named_types` / `pin_namespaces`), so the adapter only has to emit a
schema `Draft202012Validator` accepts — no `$def`-name fidelity constraint.
This also removes the four hardcoded `("x",…,"c")` 6-tuples in
`normalize_permissive()` (axis letters are 9: XYZABCUVW).

## Problem

`tools/fluidnc-config-schema.json` (~3,500 hand-maintained lines) and
`FluidNC/docs/config_items.yaml` (generated from `// @config` source
annotations by `tools/build_config_docs.py`) encode the same information in
two serializations. The JSON file is a hand-written derivative and it has
drifted — descriptions, and structural facts too (e.g. it classed `tmc_2160`
as a raw-register driver when it is an alias of `tmc_5160`; it constrained
axis letters to `x y z a b c` when the firmware has nine). It has also
*deliberately* forked (dropped `rgbled:`, marked `extenders:` deprecated)
on editorial judgement not present in source.

`ItemDocs.md` already names the target under "What this feeds → 1. Replace
the hand/LLM-maintained `fluidnc-config-schema.json`".

## Approach

`config_items.yaml` becomes the single source of truth.
`tools/fluidnc_validate_core.py::load_schema()` reads it and an **adapter**
builds an in-memory JSON-Schema dict from it at load time.
`Draft202012Validator` stays the validation engine. The JSON file is
deleted. Two small things `@config` annotations genuinely cannot express are
handled explicitly: the pin-string attribute grammar (a constant in the
adapter) and the pin-type namespace set (a new source annotation).

---

## Decisions (settled)

1. **Governing rule: present in `config_items.yaml` ⇒ accepted.** No
   deprecation / stability / lifecycle concept anywhere in the validator. A
   feature that should not be accepted must be removed from source so it
   drops out of `config_items.yaml`. Consequences: `rgbled:`, `extenders:`,
   `pinextenderN`, and `pinext…` pin strings all simply validate.

2. **Deprecation machinery removed.** `scan_deprecated()` in
   `fluidnc_validate_core.py`, its call site in `validate_document()`, the
   `$defs/pinDeprecated` definition, and all `deprecated: true` handling are
   deleted. The "deprecated-feature usage is always a warning" paragraph is
   removed from the CLI and MCP docstrings.

3. **Pin-type namespaces come from a new annotation.** `// @pin_namespace
   <token>` is placed on each `*PinDetail` subclass in `src/Pins/`, at the
   site where that detail's prefix literal is set (model: `ExtPinDetail.cpp`
   `_name = "pinext"`). `gpio` stays builtin in the adapter (no annotation).
   The annotation carries the index shape:
   - flat namespace: `// @pin_namespace i2so` → `i2so\.[0-9]+`
   - instance-numbered namespace: `// @pin_namespace uart_channel<n>` →
     `uart_channel[0-9]\.[0-9]+`; `// @pin_namespace pinext<n>` →
     `pinext[0-9]\.[0-9]+`
   The numbered/flat distinction and canonical spelling must be cross-checked
   against the `pin_type` dispatch chain in `Pin.cpp` (~L68–125); a CI check
   comparing the annotation set to that chain is cheap and catches drift.

4. **Pin attribute-suffix grammar stays an adapter constant.**
   `(:(high|low|pu|pd|ds[0-3]))*` is not per-namespace and is not `@config`
   data. One constant in the adapter, mirror of `Pin::parse`; it changes
   approximately never. `NO_PIN` / `no_pin` / `void` (index-less sentinels)
   are likewise hardcoded in the adapter.

5. **Placeholder → key-pattern table is small, fixed, generator-owned.**
   Nothing in it tracks a build parameter:

   | placeholder | key pattern | rationale |
   |---|---|---|
   | `<letter>` | `[xyzabcuvw]`, case-insensitive | permanent — G-code has no more axis letters |
   | `motorN` | `motor[01]` (exact) | `MAX_MOTORS_PER_AXIS = 2` is architecturally entrenched; an out-of-range `motor2:` should be a clear error |
   | `uartN` | `uart[0-9]` | single digit is 10 slots vs 3 hardware UARTs; also structurally rejects the long-deprecated bare `uart:` subsection |
   | `uart_channelN` | `uart_channel[0-9]` | as above |
   | `i2cN` | `i2c[0-9]` | as above |
   | `<i2c_chip>` | dispatch set `{pca9539, pca9535_9555}` | enumerated child types |

6. **Generator emits per-section metadata** into `config_items.yaml` as a
   **top-level `section_meta:` map** keyed by section path, sibling to
   `spindle_sections:` / `vfd_protocol_fields:`. It carries `repeatable`,
   `key_pattern`, and (for `<i2c_chip>`) `child_types`. Sections with no
   entry are plain singletons. Free-text section notes stay as `#` comments
   above each header (human-facing); `section_meta:` is machine-facing only.

7. **`vfd_named_types` is grepped by the generator.** Named VFDs register
   uniformly as
   `SpindleFactory::DependentInstanceBuilder<VFDSpindle, …> registration("Name")`
   in `Spindles/VFD/*.cpp`. The generator extracts the names, drops
   `ModbusVFD`, emits `vfd_named_types: [...]` as a top-level list, and
   cross-checks the result against the prose list in the `ModbusVFD`
   `SECTIONS` note (warn on mismatch).

8. **CLI: `--schema-json` is dropped.** `--schema` changes meaning to "path
   to an alternate `config_items.yaml`" (e.g. `release/current/docs/…`).
   There is no raw-JSON bypass. The migration-time "adapter output vs old
   hand file" comparison is a one-off dev script, not a shipped flag.

---

## Work item 1 — source annotations

**`// @pin_namespace <token>`** on the `*PinDetail` subclasses in
`src/Pins/`:

| subclass | annotation | resulting fragment |
|---|---|---|
| `I2SOPinDetail` | `@pin_namespace i2so` | `i2so\.[0-9]+` |
| `UartChannelPinDetail` | `@pin_namespace uart_channel<n>` | `uart_channel[0-9]\.[0-9]+` |
| `ExtPinDetail` | `@pin_namespace pinext<n>` | `pinext[0-9]\.[0-9]+` |
| `GPIOPinDetail` | *(none — builtin)* | `gpio\.[0-9]+` (adapter constant) |
| `VoidPinDetail` | *(none — builtin)* | `void` (adapter constant) |

`<n>` expands the same way as the section placeholder table (single digit).
Annotating `GPIOPinDetail`/`VoidPinDetail` too, to remove the adapter
special-case, is an acceptable alternative if preferred at implementation
time.

No other new annotation grammar. `@stability` is **not** added (decision 1).

---

## Work item 2 — generator (`tools/build_config_docs.py`)

1. **`SECTIONS` 3rd tuple element** becomes an optional dict
   (`{"note": ..., ...}`); a bare string is shimmed to `{"note": str}` for
   back-compat. Not strictly required by any remaining decision, but it is
   the natural home for anything section-level the table needs to express.
2. **Placeholder → `key_pattern`** using the fixed table in decision 5.
   Emit `section_meta:` (decision 6) for every section whose path contains a
   placeholder (`<letter>`, `N` suffix, `<i2c_chip>`), plus `repeatable:
   true`.
3. **`<i2c_chip>` `child_types`**: grep `Extenders/*.cpp` for
   `PinExtenderFactory::InstanceBuilder<…> registration("name")` (model:
   `PCA9539.cpp`), emit as `child_types` on the pin-extender `section_meta`
   entry.
4. **`pin_namespaces:`** top-level map, built from the `@pin_namespace`
   annotations, e.g.

   ```yaml
   pin_namespaces:
     i2so:         { pattern: 'i2so\.[0-9]+' }
     uart_channel: { pattern: 'uart_channel[0-9]\.[0-9]+' }
     pinext:       { pattern: 'pinext[0-9]\.[0-9]+' }
   ```

   (`gpio` / `void` are added by the adapter.)
5. **`vfd_named_types:`** top-level list (decision 7) with its cross-check.
6. **CI check**: annotation-derived `pin_namespaces` vs the `Pin.cpp`
   `pin_type` dispatch chain; warn on any prefix in one and not the other.

All of the above is additive to `config_items.yaml` — existing consumers
ignore unknown top-level keys.

---

## Work item 3 — the adapter (`tools/config_schema_adapter.py`, new)

```python
def build_schema(config_items: dict) -> dict:
    """config_items.yaml (parsed) -> Draft-2020-12 schema dict,
    structurally equivalent to the retired fluidnc-config-schema.json."""
```

Pure translator: no embedded FluidNC knowledge beyond the two hardcoded pin
constants (decision 4) and the `gpio`/`void` builtins.

### Responsibilities

1. **Type map**: `string→{type:string}`, `integer→{type:integer}`,
   `float→{type:number}`, `boolean→{$ref:#/$defs/boolean}`,
   `enum→{type:string, enum:<resolved list>}` (PyYAML resolves the
   `enum_types` anchors on load), `pin→{$ref:#/$defs/pinAny}`. Attach
   `minimum`/`maximum` from `min`/`max` when present. `default`,
   `default_note`, `tuning`, `unit`, `pin_attributes` are **ignored** for
   validation (the current validator does not check them either).
2. **Section → object schema**: `{type:[object,null],
   additionalProperties:false, properties:{…}}`. `[object,null]` because an
   empty `section:` parses to `None`. `additionalProperties:false` is the
   global default — the generator covers every field of every section from
   verified contributors, so "closed everywhere" is now safe (and stricter
   than the retired file, which left some hand-unverified sections open).
3. **Nesting from dotted paths**:
   - `axes` → group-level items + `patternProperties:{'^[xyzabcuvw]$'
     (case-insensitive): axisLetter}`
   - `axisLetter` → `axes.<letter>` items + `homing: homingBlock` +
     `patternProperties:{'^motor[01]$': motorBlock}`
   - `motorBlock` → `axes.<letter>.motorN` shared items + one optional
     property per driver-type section (`standard_stepper` … `null_motor`),
     each → that section's object schema
4. **Spindle dispatch**: one root `properties` entry per name in
   `spindle_sections:` → that section's schema. One root entry per name in
   `vfd_named_types:` → `ModbusVFD` schema minus the `vfd_protocol_fields`
   keys.
5. **Kinematics dispatch**: `kinematics` object, one optional property per
   `kinematics.*` section; `midtbot` / `Cartesian` → empty object.
6. **Numbered map sections**: `uartN` / `uart_channelN` / `i2cN` → root
   `patternProperties` from `section_meta[...].key_pattern`; `pinextenderN`
   → `patternProperties` under `extenders`, with its `child_types` as the
   inner dispatch.
7. **`(top-level machine items)`** → items merged directly into root
   `properties`.
8. **`$defs/pinAny`**: `anyOf` of `NO_PIN|no_pin` (literal),
   `void` (case-insensitive), `gpio\.[0-9]+`, and each `pin_namespaces[*]`
   pattern — each optionally followed by the attribute-suffix constant.
9. **`$defs` name compatibility (largest single task).**
   `fluidnc_validate_core.py` reaches into `$defs` by hardcoded name
   (`_def()`, `schema["$defs"][…]`, `_resolve_ref()`), currently including
   at least: `boolean`, `steppingSection`, `axesSection`, `axisLetter`,
   `motorBlock`, `homingBlock`, `extendersSection`, `kinematicsSection`,
   `uartSection`, `uartChannelSection`, `i2cSection`, `usb_host`. **First
   implementation step: grep every such reference, produce the definitive
   list, and have `build_schema` emit each `$def` under exactly that name**,
   with root/section schemas `$ref`-ing them. Then `normalize_permissive()`
   needs zero changes. (`pinDeprecated` is removed from that list per
   decision 2.)

### Guards to port from the generator

- Skip `SECTIONS` pseudo-entries whose name contains `" / "` or parens
  (`kinematics.midtbot / kinematics.Cartesian`, `(top-level machine items)`
  handled specially) — `build_config_docs.py::main()` already does this.
- Empty-contributor entries that are still real keys (`null_motor`,
  `NoSpindle`) → empty closed object.

---

## Work item 4 — core (`tools/fluidnc_validate_core.py`)

- `load_schema(path=DEFAULT_CONFIG_ITEMS_PATH)` — same signature and return
  type; new body loads YAML and calls `config_schema_adapter.build_schema`.
- Delete `scan_deprecated()` and its call in `validate_document()`.
- Everything else unchanged: `_stringify_keys`, `check_common_mistakes`,
  `normalize_permissive`, `ENUM_FIELDS`, `Draft202012Validator` wiring.
- `normalize_permissive()` keeps its `extenders.pinextenderN.<type>` casing
  branch (still a valid section).

---

## Work item 5 — CLI + MCP

- `validate_fluidnc_config.py`: drop `--schema-json`; `--schema` now names an
  alternate `config_items.yaml`. Update the module docstring (remove the
  deprecated-warning paragraph; restate scope).
- `fluidnc_config_mcp_server.py`: repoint `SCHEMA_PATH` →
  `FluidNC/docs/config_items.yaml`; `_get_schema()` calls the adapter;
  update docstrings.

---

## Work item 6 — migration & verification

1. Land work items 1–2, regenerate `config_items.yaml`, eyeball the new
   `section_meta:` / `pin_namespaces:` / `vfd_named_types:` output.
2. **Golden-diff dev script** (not shipped): `build_schema(load
   config_items.yaml)` vs the committed `fluidnc-config-schema.json`, deep
   diff, triage every delta as *drift-fix* or *adapter bug*. Expected
   drift-fixes:
   - `<letter>` gains `u v w`
   - `tmc_2160` gains the full SPI field set (alias of `tmc_5160`)
   - `rgbled:` reappears as a valid section
   - per-field `description` strings vanish (unused by validation)
   - some `additionalProperties` flip from open to closed
   - `homing_amps` / `stallguard_seek` / `safety_polling` already present
     (added to the hand file in the branch that motivated this plan)
   - `debug` default `1` (was stale `2` in places)
3. **Corpus test**: run the old JSON and the adapted schema through
   `Draft202012Validator` over every board/example `*.yaml` in the repo plus
   known-good and known-bad fixtures; assert identical `valid` verdicts,
   triage any difference.
4. Repoint MCP + CLI; run their existing tests.
5. Delete `tools/fluidnc-config-schema.json` once 2 and 3 are green. Update
   `ItemDocs.md` "What this feeds" to mark item 1 done. Remove the
   `fluidnc-config-schema.json` mentions from `build_config_docs.py`
   comments and `SPINDLE_SECTIONS` / `vfd_protocol_fields()` docstrings.

---

## Out of scope

- `tools/fluidnc-config-spec.md` (the human narrative spec). It can become a
  second generator output later; for now it stays hand-authored. This plan
  does not touch it.
- Cross-field structural rules the JSON schema never encoded either
  (`limit_all_pin` vs `limit_neg_pin` exclusivity, "`i2so:` required if any
  `i2so.N` pin used"). Still prose-only.

---

## Open implementation questions (non-blocking)

- Final `$defs` name inventory (work item 3, step 9) — resolve by grep at
  implementation start.
- Whether any `*PinDetail` subclass carrying `@pin_namespace` is behind an
  `#ifdef` the generator's flat scan cannot resolve (the pin-type chain in
  `Pin.cpp` is not conditional today, so likely a non-issue).
- Whether to annotate `GPIOPinDetail` / `VoidPinDetail` as well and drop the
  adapter builtins (cosmetic consistency vs two fewer annotations).

---

## Effort

- Work items 1–2 (annotations + generator): ~1 day.
- Work item 3 (adapter): ~1–2 days, most of it the `$defs`-name inventory
  and getting `motorBlock` / `axisLetter` / kinematics nesting
  byte-equivalent to today.
- Work items 4–6 (core, CLI/MCP, migration): ~1 day.

Net: ~3,500 hand-maintained JSON lines → ~250 adapter lines + ~40 generator
lines + a handful of one-line source annotations. Types, ranges, enums, and
key sets stop being hand-copied.
