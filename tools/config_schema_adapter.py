"""
config_schema_adapter.py -- build a JSON Schema (Draft 2020-12) for a
FluidNC config.yaml from the generated FluidNC/docs/config_items.yaml.

This replaces the hand-maintained tools/fluidnc-config-schema.json.
config_items.yaml is generated from the // @config source annotations by
tools/build_config_docs.py, so it -- not a hand-kept parallel file -- is the
single source of truth for every config item's type, range and enum values,
and (via its section_meta / pin_namespaces / vfd_named_types / spindle_sections
blocks) for the structural shape of the document.

The output is consumed by fluidnc_validate_core.py, which runs it through
jsonschema's Draft202012Validator exactly as it did the old hand file. It is
NOT written to disk as a build artifact -- it is built in memory at load time.

Two things config_items.yaml legitimately cannot express, kept here as small
hand-owned constants (they are runtime parse-grammar, not @config metadata):

  * the pin-string attribute suffix `(:high|:low|:pu|:pd|:ds0-3)*`
  * the `no_pin` / `void` index-less sentinels, and `gpio` as the one builtin
    pin namespace (every other namespace comes from a @pin_namespace
    annotation, surfaced in config_items.yaml's pin_namespaces block)

See tools/PLAN-config-schema-adapter.md.
"""

# --- hand-owned constants (see module docstring) ---------------------------

# Axis letters are permanently capped at the G-code alphabet.
AXIS_LETTERS = ["x", "y", "z", "a", "b", "c", "u", "v", "w"]

# Pin attribute suffix, mirror of Pins::PinOptionsParser. Case-insensitive to
# match the real parser (opt.is() is strncasecmp-based).
_PIN_ATTR_SUFFIX = r"(?::(?i:high|low|pu|pd|ds[0-3]))*"

# Primitive value $defs whose patterns mirror runtime parse grammar, not
# anything expressible as an @config range. Copied verbatim from the retired
# hand schema so behaviour is unchanged.
_PRIMITIVE_DEFS = {
    "boolean": {"type": "boolean"},
    "uartData": {"type": "string", "pattern": r"^[5-8][NnEeOo][12]$"},
    "floatArray": {
        "type": "string",
        "pattern": r"^-?[0-9]+(\.[0-9]+)?(\s+-?[0-9]+(\.[0-9]+)?)*$",
    },
    "speedMap": {
        "type": "string",
        "pattern": r"^[0-9]+=[0-9]+(\.[0-9]+)?%?(\s+[0-9]+=[0-9]+(\.[0-9]+)?%?)*$",
    },
    "macroLine": {"type": ["string", "null"]},
}

# config_items.yaml `type:` value -> how to render it as a schema node.
# Anything not listed (plain "string", the rgbled hex-colour label, ...) is a
# bare string.
_REF_TYPES = {
    "boolean": "boolean",
    "pin": "pinAny",
    "uart_mode": "uartData",
    "std::vector<float>": "floatArray",
    "std::vector<Configuration::speedEntry>": "speedMap",
    "macro": "macroLine",
}

# Top-level keys of config_items.yaml that are metadata blocks, not sections.
_META_KEYS = {
    "enum_types",
    "spindle_sections",
    "vfd_protocol_fields",
    "vfd_named_types",
    "pin_namespaces",
    "section_meta",
}

_TOP_LEVEL_ITEMS = "(top-level machine items)"


def _pin_any(pin_namespaces):
    alts = [r"no_pin", r"void", r"gpio\.[0-9]+"]
    alts += [pin_namespaces[k]["pattern"] for k in sorted(pin_namespaces)]
    return {
        "type": "string",
        "pattern": r"^(?i:" + "|".join(alts) + r")" + _PIN_ATTR_SUFFIX + r"$",
    }


def _field_schema(field):
    """One config item entry -> a schema node."""
    t = field.get("type")
    if t == "integer" or t == "float":
        node = {"type": "integer" if t == "integer" else "number"}
        if "min" in field:
            node["minimum"] = field["min"]
        if "max" in field:
            node["maximum"] = field["max"]
        return node
    if t == "enum":
        vals = list(field["values"])
        node = {"enum": vals}
        if all(isinstance(v, str) for v in vals):
            node["type"] = "string"
        elif all(isinstance(v, int) and not isinstance(v, bool) for v in vals):
            node["type"] = "integer"
        return node
    if t == "axis":
        return {"type": "string", "enum": list(AXIS_LETTERS)}
    ref = _REF_TYPES.get(t)
    if ref:
        return {"$ref": f"#/$defs/{ref}"}
    return {"type": "string"}


def _obj(fields):
    """A closed object schema from a {name: entry} map (or None -> no fields)."""
    props = {name: _field_schema(f) for name, f in (fields or {}).items()}
    return {
        "type": ["object", "null"],
        "additionalProperties": False,
        "properties": props,
    }


def build_schema(ci: dict) -> dict:
    """config_items.yaml (parsed) -> Draft 2020-12 schema dict."""
    sections = {k: v for k, v in ci.items() if k not in _META_KEYS}

    section_meta = ci.get("section_meta", {})
    spindle_sections = ci.get("spindle_sections", [])
    vfd_named = ci.get("vfd_named_types", [])
    vfd_protocol_fields = set(ci.get("vfd_protocol_fields", []))

    defs = dict(_PRIMITIVE_DEFS)
    defs["pinAny"] = _pin_any(ci.get("pin_namespaces", {}))

    def key_re(path):
        """`^<key_pattern>$` for a repeated section path, from section_meta."""
        return "^" + section_meta[path]["key_pattern"] + "$"

    # --- axes: group fields + per-letter blocks --------------------------
    driver_sections = sorted(
        k for k in sections if k.startswith("axes.<letter>.motorN.")
    )
    motor_block = _obj(sections.get("axes.<letter>.motorN"))
    for dpath in driver_sections:
        motor_block["properties"][dpath.split(".")[-1]] = _obj(sections[dpath])

    axis_letter = _obj(sections.get("axes.<letter>"))
    axis_letter["properties"]["homing"] = _obj(sections.get("axes.<letter>.homing"))
    axis_letter["patternProperties"] = {
        key_re("axes.<letter>.motorN"): motor_block
    }

    axes_obj = _obj(sections.get("axes"))
    axes_obj["patternProperties"] = {key_re("axes.<letter>"): axis_letter}

    # --- kinematics dispatch -------------------------------------------------
    kin_obj = {
        "type": ["object", "null"],
        "additionalProperties": False,
        "properties": {
            k.split(".", 1)[1]: _obj(sections[k])
            for k in sorted(sections)
            if k.startswith("kinematics.")
        },
    }

    # --- extenders: pinextenderN -> {chip: fields} -------------------------
    chip_fields = sections.get("extenders.pinextenderN.<i2c_chip>")
    child_types = section_meta.get("extenders.pinextenderN.<i2c_chip>", {}).get(
        "child_types", []
    )
    ext_instance = {
        "type": ["object", "null"],
        "additionalProperties": False,
        "properties": {chip: _obj(chip_fields) for chip in child_types},
    }
    extenders_obj = {
        "type": ["object", "null"],
        "additionalProperties": False,
        "patternProperties": {key_re("extenders.pinextenderN"): ext_instance},
    }

    # --- root properties --------------------------------------------------
    props = {}

    for name, field in (sections.get(_TOP_LEVEL_ITEMS) or {}).items():
        if field.get("type") == "string" and name == "meta":
            props[name] = {}  # deliberately unconstrained, matches retired schema
        else:
            props[name] = _field_schema(field)

    NESTED = {
        "axes": axes_obj,
        "kinematics": kin_obj,
        "extenders": extenders_obj,
    }
    NUMBERED = {"uartN": "^uart[0-9]$", "uart_channelN": "^uart_channel[0-9]$", "i2cN": "^i2c[0-9]$"}

    # A "<section>.<sub>" path (that isn't a placeholder repeat) is a nested
    # singleton subsection, e.g. uartN.usb_host -- attach it as a property of
    # its parent's object schema.
    subsections = {}  # parent -> {sub: schema}
    for key, body in sections.items():
        if "." not in key or key.startswith(("axes.", "kinematics.", "extenders.")):
            continue
        parent, sub = key.rsplit(".", 1)
        if parent in sections and "<" not in sub and not sub.endswith("N"):
            subsections.setdefault(parent, {})[sub] = _obj(body)

    pattern_props = {}
    for key, body in sections.items():
        if key in (_TOP_LEVEL_ITEMS,) or key in NESTED:
            continue
        if key.startswith("axes.") or key.startswith("kinematics.") or key.startswith("extenders."):
            continue
        if "." in key and key.rsplit(".", 1)[0] in sections:
            continue  # a subsection, attached to its parent below
        obj = _obj(body)
        obj["properties"].update(subsections.get(key, {}))
        if key in NUMBERED:
            pattern_props[key_re(key)] = obj
        else:
            props[key] = obj  # plain singleton (start, stepping, PWM, rgbled, ...)

    props.update(NESTED)

    for name in vfd_named:
        props[name] = _obj(
            {k: v for k, v in (sections.get("ModbusVFD") or {}).items()
             if k not in vfd_protocol_fields}
        )

    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "title": "FluidNC config.yaml",
        "description": (
            "Generated from FluidNC/docs/config_items.yaml by "
            "tools/config_schema_adapter.py; do not edit. Validates the "
            "parsed (YAML->JSON) representation of a config file. Board pin "
            "legality is intentionally not checked."
        ),
        "type": "object",
        "properties": props,
        "patternProperties": pattern_props,
        "additionalProperties": False,
        "$defs": defs,
    }


if __name__ == "__main__":
    import json
    import sys
    import yaml

    src = sys.argv[1] if len(sys.argv) > 1 else "FluidNC/docs/config_items.yaml"
    with open(src) as f:
        print(json.dumps(build_schema(yaml.safe_load(f)), indent=2))
