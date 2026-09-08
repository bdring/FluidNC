"""
fluidnc_validate_core.py — shared validation core for FluidNC config.yaml,
used by both validate_fluidnc_config.py (CLI) and fluidnc_config_mcp_server.py.

The validation schema is built at load time from FluidNC/docs/config_items.yaml
(generated from the // @config source annotations) by config_schema_adapter.py.
config_items.yaml is the single source of truth: a section or field is valid
iff it appears there. There is no deprecation/lifecycle concept -- to stop
accepting something, remove its annotations from the firmware source.

Background: FluidNC's real parser matches essentially every key name (not
just "type selector" names like spindle/motor/kinematics type keys, but
ORDINARY field names too, e.g. 'm6_macro'/'M6_macro') and several enum
values case-insensitively (ground truth: Parser::is(), strncasecmp-based).
The schema enforces one canonical casing, on the theory that guiding
generation toward a single consistent form is more useful than accepting
every casing variant. Validating existing/human-written configs against
that canonical-only stance is too strict in practice (mixed casing is
common and harmless there) -- hence two modes:

  STRICT mode (default):     validate the document as-is against the schema.
                              Casing mismatches are reported as errors.
  PERMISSIVE mode:           every key name and known enum value is
                              case-insensitively matched against the
                              schema's own canonical spelling and normalized
                              before validation; each normalization is
                              recorded as a WARNING, not an error. A file
                              that only has casing mismatches will validate
                              cleanly in this mode, with warnings explaining
                              what was normalized.

Design note: canonical names are read from the schema at runtime, not
duplicated into hand-maintained Python lists -- so this module tracks
config_items.yaml automatically as sections/fields are added.
"""
import copy
import re
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError:
    Draft202012Validator = None  # caller is responsible for a clear error message


# field-name -> allowed canonical enum values, matched wherever that field
# name appears as a leaf string value anywhere in the document. A small
# hand list rather than schema-derived: pulling "which string properties are
# closed enums" back out of the schema would barely simplify this.
ENUM_FIELDS = {
    "engine": ["Timed", "RMT", "I2S_STATIC", "I2S_STREAM", "Simulator", "PIO"],
    "run_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "homing_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "message_level": ["None", "Error", "Warn", "Info", "Debug", "Verbose"],
    "axis": ["x", "y", "z", "a", "b", "c", "u", "v", "w"],  # parking.axis
}


def _canonical_key_match(key, canonical_names):
    """Return the canonical name if key case-insensitively matches one of
    canonical_names but isn't already an exact match; else None.

    Defensively handles non-string keys: YAML 1.1 (which PyYAML follows)
    parses bare on/off/yes/no/true/false/y/n as booleans even when used as a
    mapping KEY, not just a value -- e.g. a GitHub Actions workflow file's
    `on:` key parses to the Python bool True, not the string "on". A
    document like that should never reach this function in practice (see
    the CI workflow's own path filtering), but this function must not crash
    if it ever does -- a non-string key simply can't case-insensitively
    match a canonical (string) name, so there's nothing to normalize."""
    if not isinstance(key, str):
        return None
    if key in canonical_names:
        return None
    for name in canonical_names:
        if key.lower() == name.lower():
            return name
    return None


def _canonical_value_match(value, canonical_values):
    if not isinstance(value, str) or value in canonical_values:
        return None
    for v in canonical_values:
        if value.lower() == v.lower():
            return v
    return None


def _obj_props(node):
    """Canonical property names of an adapter-emitted object schema node.
    The adapter inlines every section/type block's fields directly under
    "properties" (only value primitives are $ref), so no ref resolution is
    needed here."""
    return list(node.get("properties", {}).keys()) if isinstance(node, dict) else []


def normalize_permissive(doc, schema):
    """
    Walk a parsed FluidNC config document, renaming every key that
    case-insensitively matches a canonical name in `schema` (the adapter
    output built from config_items.yaml) to that canonical casing, and
    likewise for known enum leaf values. Returns (normalized_doc, warnings);
    the input is not mutated.

    FluidNC's real parser is case-insensitive on essentially every key,
    type-selector name and several enum values (Parser::is(), strncasecmp);
    the schema is canonical-only. This pass lets a human-written config with
    only casing differences validate cleanly, each rename recorded as a
    warning.

    The adapter emits a regular tree -- section objects carry their fields
    inline under "properties", repeated sections (axis letters, motorN,
    pinextenderN) under "patternProperties", spindle/kinematics/motor-driver
    type blocks are just nested objects -- so one generic recursive walk
    covers every case (no per-section special handling, unlike the version
    that read the old hand schema's $defs).
    """
    doc = copy.deepcopy(doc)
    warnings = []

    def warn(path, message):
        warnings.append({"path": list(path), "message": message})

    def rename_keys(node, path, canonical_keys, label, skip_keys=()):
        if not isinstance(node, dict):
            return
        for k in list(node.keys()):
            if k in skip_keys:
                continue
            canon = _canonical_key_match(k, canonical_keys)
            if canon:
                warn(path + [k], f"{label} '{k}' normalized to canonical casing '{canon}' "
                                 f"(case-insensitive match; real firmware accepts either)")
                node[canon] = node.pop(k)

    def child_schema(obj_schema, key):
        """Schema node for `key` within an object schema: an exact/ci
        'properties' entry, else the first matching 'patternProperties'."""
        props = obj_schema.get("properties", {})
        if key in props:
            return props[key]
        for name, sub in props.items():
            if isinstance(key, str) and key.lower() == name.lower():
                return sub
        for rx, sub in obj_schema.get("patternProperties", {}).items():
            if isinstance(key, str) and re.match(rx, key, re.IGNORECASE):
                return sub
        return None

    def recurse(node, path, obj_schema):
        if not isinstance(node, dict) or not isinstance(obj_schema, dict):
            return
        rename_keys(node, path, _obj_props(obj_schema),
                    "key" if len(path) else "top-level key")
        for k, v in list(node.items()):
            sub = child_schema(obj_schema, k)
            if isinstance(sub, dict) and ("properties" in sub or "patternProperties" in sub):
                recurse(v, path + [k], sub)

    def normalize_enum_leaves(node, path):
        if isinstance(node, dict):
            for k, v in list(node.items()):
                if k in ENUM_FIELDS and isinstance(v, str):
                    canon = _canonical_value_match(v, ENUM_FIELDS[k])
                    if canon:
                        warn(path + [k], f"'{v}' normalized to canonical casing '{canon}' "
                                         f"(case-insensitive match; real firmware accepts either)")
                        node[k] = canon
                normalize_enum_leaves(v, path + [k])
        elif isinstance(node, list):
            for i, item in enumerate(node):
                normalize_enum_leaves(item, path + [i])

    if isinstance(doc, dict):
        recurse(doc, [], schema)
    normalize_enum_leaves(doc, [])
    return doc, warnings


def load_schema(config_items_path: Path) -> dict:
    """Build the validation schema from config_items.yaml via the adapter.
    (Was: load the hand-maintained fluidnc-config-schema.json.)"""
    import yaml

    import config_schema_adapter

    with open(config_items_path) as f:
        return config_schema_adapter.build_schema(yaml.safe_load(f))


def _stringify_keys(node):
    """
    Recursively convert every dict key to a string, returning a new
    structure (input is not mutated).

    This exists because YAML (1.1, which PyYAML follows) parses certain
    bare words as booleans/null even when used as a mapping KEY, not just a
    value -- most notably `on`/`off`/`yes`/`no`/`true`/`false`/`y`/`n`
    (classic example: a GitHub Actions workflow's `on:` key parses to the
    Python bool True). JSON itself has no such concept -- object keys are
    always strings -- and the `jsonschema` package was built against that
    assumption: its patternProperties matching does a regex match directly
    against each key, which raises an uncaught TypeError if the key isn't a
    string, before our own validation or normalization code ever runs.

    Called unconditionally, on every document, before anything else in
    validate_document() -- not just as a defensive measure for obviously
    wrong input like a workflow file (which shouldn't reach this function at
    all if callers filter their inputs correctly), but because a config.yaml
    a user actually intends could plausibly contain a bare `on:`/`off:` key
    by mistake, and this validator should report that clearly (as an
    unrecognized top-level key, which is what it becomes once stringified)
    rather than crash.
    """
    if isinstance(node, dict):
        return {str(k): _stringify_keys(v) for k, v in node.items()}
    if isinstance(node, list):
        return [_stringify_keys(v) for v in node]
    return node


def check_common_mistakes(doc):
    """
    Detect specific, confirmed AI-generation mistakes and return sharp,
    didactic error messages for them -- distinct from (and IN ADDITION TO)
    whatever generic schema-violation message jsonschema itself produces for
    the same key, since a generic "'spindles' does not match any of the
    regexes: ..." message does not explain what to do about it.

    Currently checks for exactly one confirmed pattern: a top-level
    'spindle'/'spindles' wrapper key (any casing, singular or plural) around
    spindle content. Ground truth: three different AI assistants (Claude,
    GitHub Copilot, and Gemini), independently, have each invented some
    version of this wrapper -- it does not exist in any form. See spec §10
    for the full writeup of why this is such a strong, convergent mistake.

    Returns a list of error dicts, same shape as jsonschema-derived errors
    ({"path": [...], "message": "..."}). Always runs, regardless of
    strict/permissive mode -- this is a structural impossibility, not a
    casing-leniency question.
    """
    errors = []
    if not isinstance(doc, dict):
        return errors

    for key, value in doc.items():
        if not isinstance(key, str) or key.lower() not in ("spindle", "spindles"):
            continue

        suggestion = ""
        if isinstance(value, dict):
            nested_keys = list(value.keys())
            if "type" in value and isinstance(value.get("type"), str):
                spindle_type = value["type"]
                suggestion = (
                    f" It looks like you wrote a 'type: {spindle_type}' field -- instead, "
                    f"move '{key}' out entirely and rename it to the type name itself, e.g. "
                    f"a top-level 'Relay:' key (capitalized to match the canonical spelling), "
                    f"with the rest of {value!r}'s fields (minus 'type') moved directly under it."
                )
            elif nested_keys:
                inner_key = nested_keys[0]
                suggestion = (
                    f" It looks like '{inner_key}:' is nested inside '{key}:' -- instead, move "
                    f"'{inner_key}:' up to be its own top-level key, at the same level as "
                    f"'axes:'/'control:'/etc., and delete the '{key}:' wrapper entirely."
                )
        elif isinstance(value, list):
            suggestion = (
                f" It looks like '{key}:' holds a list -- FluidNC has no such list. Take "
                f"each list item's spindle type name (from a 'type:' field, if present) and "
                f"turn it into its own top-level key instead, e.g. a top-level 'Relay:' key, "
                f"with that item's other fields moved directly under it. Delete the "
                f"'{key}:' wrapper entirely."
            )

        errors.append({
            "path": [key],
            "message": (
                f"'{key}' is not a valid key -- FluidNC has no spindle wrapper of any kind "
                f"(not 'spindle:', 'Spindle:', or 'spindles:', singular or plural). This is a "
                f"confirmed, recurring AI-generation mistake (see spec §10) -- a spindle type "
                f"name (PWM, Relay, ModbusVFD, etc.) must be its own top-level key, exactly "
                f"like 'axes:' or 'control:', never nested inside anything else."
                + suggestion
            ),
        })

    return errors


def validate_document(doc, schema: dict, permissive: bool = False) -> dict:
    """
    Validate a parsed FluidNC config document against schema.

    Returns {"valid": bool, "errors": [...], "warnings": [...]}.
    warnings is non-empty only in permissive mode (casing-normalization
    notes). There is no deprecation concept: a section/field is either
    present in config_items.yaml -- and therefore accepted -- or it is not.
    """
    if Draft202012Validator is None:
        raise RuntimeError("jsonschema package is not installed")

    if doc is None:
        doc = {}
    doc = _stringify_keys(doc)

    mistake_errors = check_common_mistakes(doc)

    warnings = []
    if permissive:
        doc, warnings = normalize_permissive(doc, schema)

    validator = Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
    all_errors = mistake_errors + [{"path": list(e.absolute_path), "message": e.message} for e in errors]
    return {
        "valid": len(all_errors) == 0,
        "errors": all_errors,
        "warnings": warnings,
    }
