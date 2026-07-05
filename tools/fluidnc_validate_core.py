"""
fluidnc_validate_core.py — shared validation core for FluidNC config.yaml,
used by both validate_fluidnc_config.py (CLI) and fluidnc_config_mcp_server.py.

Background: FluidNC's real parser matches essentially every key name (not
just "type selector" names like spindle/motor/kinematics type keys, but
ORDINARY field names too, e.g. 'm6_macro'/'M6_macro') and several enum
values case-insensitively (ground truth: Parser::is(), strncasecmp-based).
The JSON Schema (fluidnc-config-schema.json) deliberately does NOT replicate
that leniency -- it enforces one canonical casing, on the theory that
guiding generation toward a single consistent form is more useful than
accepting every casing variant. But validating existing/human-written
configs against that canonical-only stance is too strict in practice (mixed
casing is common and harmless there) -- hence two modes:

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

Design note: canonical names are read directly from the loaded JSON Schema
at runtime (via schema["properties"]/schema["$defs"][...]["properties"] and
"$ref" targets) rather than duplicated into hand-maintained Python lists.
This was a deliberate fix -- an earlier version of this module kept its own
parallel lists of spindle/motor/kinematics type names, which would have
silently drifted out of sync with the schema over time. Reading them from
the schema means this module extends automatically as the schema grows.

Separately from strict/permissive: this module also scans for usage of
anything the schema marks "deprecated": true (currently: the extenders:/
rgbled: sections, and pinext-syntax pin values) and reports it as a warning,
REGARDLESS of mode -- see scan_deprecated(). Deprecation is orthogonal to
the casing-strictness question above; a deprecated-but-otherwise-valid
config is worth flagging either way.
"""
import copy
import json
import re
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError:
    Draft202012Validator = None  # caller is responsible for a clear error message


# field-name -> allowed canonical enum values, matched wherever that field
# name appears as a leaf string value anywhere in the document. This one
# piece is still a hand-maintained list (rather than schema-derived) because
# extracting "which string properties are semantically closed enums" back
# out of the compiled schema's "enum" keyword would be only a marginal
# simplification over just listing the handful of fields that need it.
ENUM_FIELDS = {
    "engine": ["Timed", "RMT", "I2S_STATIC", "I2S_STREAM"],
    "run_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "homing_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "message_level": ["None", "Error", "Warn", "Info", "Debug", "Verbose"],
    "axis": ["x", "y", "z", "a", "b", "c"],  # parking.axis
}

# Top-level keys handled by patternProperties (numbered sections) rather
# than fixed "properties" entries -- excluded from generic root key
# normalization since they're matched by regex/prefix, not by an
# enumerable canonical name.
_NUMBERED_SECTION_PATTERNS = {
    "uart": re.compile(r"^uart([0-9]+)$", re.IGNORECASE),
    "uart_channel": re.compile(r"^uart_channel([0-9]+)$", re.IGNORECASE),
    "i2c": re.compile(r"^i2c([0-9]+)$", re.IGNORECASE),
}


def _def(schema, defname):
    return schema["$defs"][defname]


def _props_of(node_schema):
    """Canonical property names of a resolved (non-$ref) schema node."""
    return list(node_schema.get("properties", {}).keys())


def _resolve_ref(schema, ref_schema):
    """Given a {'$ref': '#/$defs/X'} dict, return the resolved schema node."""
    ref = ref_schema["$ref"]
    assert ref.startswith("#/$defs/"), f"unexpected $ref form: {ref}"
    return _def(schema, ref[len("#/$defs/"):])


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


def normalize_permissive(doc, schema):
    """
    Walk a parsed FluidNC config document, normalizing every key name and
    known enum value to canonical casing per `schema`. Returns
    (normalized_doc, warnings) -- the input is not mutated.

    warnings is a list of {"path": [...], "message": "..."} dicts, same
    shape as jsonschema errors, so callers can present them uniformly.
    """
    doc = copy.deepcopy(doc)
    warnings = []

    def warn(path, message):
        warnings.append({"path": list(path), "message": message})

    def normalize_keys(node, path, canonical_keys, label, skip_keys=()):
        """Rename any key in `node` that case-insensitively matches a name
        in canonical_keys (and isn't an exact match already) to canonical
        casing. Keys in skip_keys are left alone (e.g. numbered-section
        prefixes handled separately)."""
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

    def normalize_type_selector(node, path, options_props, label):
        """`options_props` maps canonical_name -> resolved schema node for
        that type. Renames the one present key to canonical casing (if it's
        a case-insensitive-only mismatch), then normalizes fields *inside*
        the resolved type's own block, and returns the canonical name found
        (or None)."""
        if not isinstance(node, dict):
            return None
        canon_found = None
        for k in list(node.keys()):
            canon = _canonical_key_match(k, list(options_props.keys()))
            if canon:
                warn(path + [k], f"{label} '{k}' normalized to canonical casing '{canon}' "
                                  f"(case-insensitive match; real firmware accepts either)")
                node[canon] = node.pop(k)
                k = canon
            if k in options_props:
                canon_found = k
        if canon_found:
            inner = node.get(canon_found)
            if isinstance(inner, dict):
                normalize_keys(inner, path + [canon_found], _props_of(options_props[canon_found]), f"{label} field")
        return canon_found

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

    root_schema = schema
    root_props = root_schema.get("properties", {})

    # 1. Root-level fixed keys (covers board/name/meta/stepping/axes/control/
    #    coolant/probe/macros/extenders/start/parking/user_outputs/
    #    user_inputs/oled/rgbled/atc_manual/every spindle+VFD type name/the
    #    top-level scalars -- ALL of these are literal entries in
    #    schema["properties"], so one generic pass handles every one of them,
    #    including what earlier passes of this module treated as a special
    #    "spindle type" case).
    numbered_prefixes_present = set()
    for k in list(doc.keys()) if isinstance(doc, dict) else []:
        if not isinstance(k, str):
            continue
        for prefix, pattern in _NUMBERED_SECTION_PATTERNS.items():
            if pattern.match(k):
                numbered_prefixes_present.add(k)
    normalize_keys(doc, [], list(root_props.keys()), "top-level key", skip_keys=numbered_prefixes_present)

    # 1b. Normalize numbered-section prefix casing itself (e.g. "UART1" -> "uart1"),
    #     rare in practice but cheap to handle.
    if isinstance(doc, dict):
        for k in list(doc.keys()):
            if not isinstance(k, str):
                continue
            for prefix, pattern in _NUMBERED_SECTION_PATTERNS.items():
                m = pattern.match(k)
                if m and not k.startswith(prefix):
                    canon_key = prefix + m.group(1)
                    warn([k], f"numbered section '{k}' normalized to canonical casing '{canon_key}'")
                    doc[canon_key] = doc.pop(k)

    # 2. Recurse into each resolved root section that has its own nested
    #    structure, normalizing ordinary fields within.

    def normalize_flat_def(section_key, defname):
        node = doc.get(section_key)
        if isinstance(node, dict):
            normalize_keys(node, [section_key], _props_of(_def(schema, defname)), "field")

    for section_key, defname in [
        ("control", "controlSection"),
        ("coolant", "coolantSection"),
        ("probe", "probeSection"),
        ("macros", "macrosSection"),
        ("start", "startSection"),
        ("parking", "parkingSection"),
        ("user_outputs", "userOutputsSection"),
        ("user_inputs", "userInputsSection"),
        ("oled", "oledSection"),
        ("rgbled", "rgbledSection"),
        ("atc_manual", "atc_manual"),
        ("spi", "spiSection"),
        ("sdcard", "sdcardSection"),
        ("stepping", "steppingSection"),
    ]:
        normalize_flat_def(section_key, defname)

    # i2so: is defined inline under root properties, not as its own $def
    if isinstance(doc.get("i2so"), dict) and isinstance(root_props.get("i2so"), dict):
        normalize_keys(doc["i2so"], ["i2so"], _props_of(root_props["i2so"]), "field")

    # 3. Spindle/VFD type blocks: resolve each root property's $ref (if any)
    #    to get its own canonical field names.
    for key, node in list(doc.items()) if isinstance(doc, dict) else []:
        prop_schema = root_props.get(key)
        if isinstance(prop_schema, dict) and "$ref" in prop_schema and isinstance(node, dict):
            resolved = _resolve_ref(schema, prop_schema)
            normalize_keys(node, [key], _props_of(resolved), "field")

    # 4. kinematics: <type>
    kinematics_node = doc.get("kinematics")
    if isinstance(kinematics_node, dict):
        kin_options = {
            name: _resolve_ref(schema, ref) if "$ref" in ref else ref
            for name, ref in _def(schema, "kinematicsSection")["properties"].items()
        }
        normalize_type_selector(kinematics_node, ["kinematics"], kin_options, "kinematics type")

    # 5. axes: shared keys, each axis letter, homing, motorN, and each
    #    motor's resolved driver type
    axes_node = doc.get("axes")
    if isinstance(axes_node, dict):
        normalize_keys(axes_node, ["axes"], _props_of(_def(schema, "axesSection")), "field",
                        skip_keys={"x", "y", "z", "a", "b", "c"})
        axis_letter_props = _props_of(_def(schema, "axisLetter"))
        motor_options = {
            name: _resolve_ref(schema, ref)
            for name, ref in _def(schema, "motorBlock")["properties"].items()
            if "$ref" in ref
        }
        motor_block_own_props = [
            k for k in _props_of(_def(schema, "motorBlock")) if k not in motor_options
        ]
        for axis_letter in ("x", "y", "z", "a", "b", "c"):
            axis_block = axes_node.get(axis_letter)
            if not isinstance(axis_block, dict):
                continue
            normalize_keys(axis_block, ["axes", axis_letter], axis_letter_props, "field",
                            skip_keys={"homing", "motor0", "motor1"})
            homing_block = axis_block.get("homing")
            if isinstance(homing_block, dict):
                normalize_keys(homing_block, ["axes", axis_letter, "homing"],
                                _props_of(_def(schema, "homingBlock")), "field")
            for motor_key in ("motor0", "motor1"):
                motor_block = axis_block.get(motor_key)
                if isinstance(motor_block, dict):
                    normalize_keys(motor_block, ["axes", axis_letter, motor_key],
                                    motor_block_own_props, "field",
                                    skip_keys=set(motor_options.keys()) | {k.lower() for k in motor_options})
                    normalize_type_selector(
                        motor_block, ["axes", axis_letter, motor_key],
                        motor_options, "motor driver type",
                    )

    # 6. extenders.pinextenderN.<type> (deprecated feature, still normalized for completeness)
    extenders_node = doc.get("extenders")
    if isinstance(extenders_node, dict):
        extender_options_schema = _def(schema, "extendersSection")["additionalProperties"]["properties"]
        extender_options = {
            name: _resolve_ref(schema, ref) for name, ref in extender_options_schema.items()
        }
        for pe_key, pe_block in extenders_node.items():
            if isinstance(pe_block, dict):
                normalize_type_selector(pe_block, ["extenders", pe_key], extender_options, "extender type")

    # 7. uartN / uart_channelN / i2cN numbered sections (patternProperties)
    uart_section_def = _def(schema, "uartSection")
    uart_channel_def = _def(schema, "uartChannelSection")
    i2c_def = _def(schema, "i2cSection")
    usb_host_ref = uart_section_def["properties"].get("usb_host")
    usb_host_def = _resolve_ref(schema, usb_host_ref) if usb_host_ref else None

    for k, node in list(doc.items()) if isinstance(doc, dict) else []:
        if not isinstance(k, str) or not isinstance(node, dict):
            continue
        if _NUMBERED_SECTION_PATTERNS["uart_channel"].match(k):
            normalize_keys(node, [k], _props_of(uart_channel_def), "field")
        elif _NUMBERED_SECTION_PATTERNS["uart"].match(k):
            normalize_keys(node, [k], _props_of(uart_section_def), "field", skip_keys={"usb_host"})
            usb_host_node = node.get("usb_host")
            if usb_host_node is None:
                # check for a case-insensitive-only "usb_host" key
                normalize_keys(node, [k], ["usb_host"], "field")
                usb_host_node = node.get("usb_host")
            if isinstance(usb_host_node, dict) and usb_host_def:
                normalize_keys(usb_host_node, [k, "usb_host"], _props_of(usb_host_def), "field")
        elif _NUMBERED_SECTION_PATTERNS["i2c"].match(k):
            normalize_keys(node, [k], _props_of(i2c_def), "field")

    # 8. Enum-valued leaf fields, anywhere in the document
    normalize_enum_leaves(doc, [])

    return doc, warnings


def scan_deprecated(doc, schema):
    """
    Scan a document for usage of anything marked "deprecated": true in the
    schema, and return warnings describing it. Runs regardless of
    strict/permissive mode -- deprecation is a different concern from
    casing leniency (permissive mode's focus) and should surface either way,
    since a deprecated-but-syntactically-valid config is worth flagging even
    when the casing question doesn't apply.

    This closes a real gap: earlier versions of this module (and the
    schema's own pinAny description) claimed deprecated pinext pin values
    would be "flagged deprecated", but nothing actually implemented that --
    a deprecated value validated completely silently. This function is what
    makes that claim true.

    Covers two things, deliberately by explicit/targeted checks rather than
    a fully generic "walk every possible schema branch" implementation
    (which would require re-implementing oneOf/$ref resolution at every
    depth for marginal benefit given how few deprecated items currently
    exist in the schema):

      - Root-level sections whose resolved schema is deprecated (currently:
        extenders:, rgbled:) -- detected generically by resolving each
        present root key's $ref and checking its "deprecated" flag, so this
        part DOES automatically extend to any future deprecated root
        section without code changes here.
      - Pin values using the deprecated pinext syntax (spec §3.5) --
        detected by pattern-matching every string leaf value in the
        document against pinDeprecated's own regex, since pin fields exist
        in far too many locations to enumerate one by one structurally.
    """
    warnings = []
    root_props = schema.get("properties", {})

    if isinstance(doc, dict):
        for key, node in doc.items():
            prop_schema = root_props.get(key)
            if not isinstance(prop_schema, dict):
                continue
            resolved = _resolve_ref(schema, prop_schema) if "$ref" in prop_schema else prop_schema
            if resolved.get("deprecated") is True or prop_schema.get("deprecated") is True:
                desc = (resolved.get("description") or prop_schema.get("description") or "").splitlines()
                first_line = desc[0] if desc else "this section is deprecated."
                warnings.append({
                    "path": [key],
                    "message": f"'{key}' is deprecated: {first_line}",
                })

    pin_deprecated_pattern = re.compile(_def(schema, "pinDeprecated")["pattern"])

    def scan_pin_values(node, path):
        if isinstance(node, dict):
            for k, v in node.items():
                if isinstance(v, str) and pin_deprecated_pattern.match(v):
                    warnings.append({
                        "path": path + [k],
                        "message": f"'{v}' uses the deprecated pinext pin syntax (spec §3.5) -- "
                                   f"do not use this in new configs, it may be removed.",
                    })
                scan_pin_values(v, path + [k])
        elif isinstance(node, list):
            for i, item in enumerate(node):
                scan_pin_values(item, path + [i])

    scan_pin_values(doc, [])
    return warnings


def load_schema(schema_path: Path) -> dict:
    with open(schema_path) as f:
        return json.load(f)


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
    warnings can be non-empty in EITHER mode now: deprecation warnings
    (scan_deprecated) always run, regardless of permissive. In permissive
    mode, casing-normalization warnings are added on top of those.
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

    warnings = warnings + scan_deprecated(doc, schema)

    validator = Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
    all_errors = mistake_errors + [{"path": list(e.absolute_path), "message": e.message} for e in errors]
    return {
        "valid": len(all_errors) == 0,
        "errors": all_errors,
        "warnings": warnings,
    }
