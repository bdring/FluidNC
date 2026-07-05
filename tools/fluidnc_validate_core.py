"""
fluidnc_validate_core.py — shared validation core for FluidNC config.yaml,
used by both validate_fluidnc_config.py (CLI) and fluidnc_config_mcp_server.py.

Background: FluidNC's real parser matches almost every key name, type-selector
name (spindle types, motor driver types, kinematics types), and enum value
CASE-INSENSITIVELY (ground truth: Parser::is(), strncasecmp-based). The JSON
Schema (fluidnc-config-schema.json) deliberately does NOT replicate that
leniency -- it enforces one canonical casing, on the theory that guiding
generation toward a single consistent form is more useful than accepting
every casing variant. But real-world testing against a large corpus of
community-contributed configs showed this canonical-casing-only stance is too
strict for VALIDATING existing/human-written files (mixed casing is common
and harmless) -- hence two modes:

  STRICT mode (default):     validate the document as-is against the schema.
                              Casing mismatches are reported as errors.
  PERMISSIVE mode:           known case-insensitive identifiers are first
                              normalized to canonical casing (and the
                              normalization is recorded as a WARNING, not an
                              error), then the normalized document is
                              validated. A file that only has casing
                              mismatches will validate cleanly in this mode,
                              with warnings explaining what was normalized.

Only the specific fields/keys documented below are treated as
case-insensitive for normalization purposes -- this is a deliberately
targeted, structurally-scoped set (mirroring exactly what real-world testing
showed the FluidNC parser tolerates), not a blanket case-fold of the entire
document. Blanket case-folding would risk silently "fixing" things that
aren't actually case-insensitive in the real parser and would be far more
error-prone to get right.
"""
import copy
import json
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
except ImportError:
    Draft202012Validator = None  # caller is responsible for a clear error message


# ---------------------------------------------------------------------------
# Canonical casing tables (mirrors fluidnc-config-schema.json's own property
# names / enum lists -- kept in sync by hand since the schema doesn't itself
# encode "this enum is case-insensitive at runtime" as machine-readable data)
# ---------------------------------------------------------------------------

SPINDLE_TYPE_NAMES = [
    "PWM", "10V", "DAC", "HBridge", "Laser", "Relay", "OnOff", "BESC",
    "PlasmaSpindle", "NoSpindle", "ModbusVFD",
    "Huanyang", "H2A", "YL620", "DeltaMS300", "FolinnBD600", "H100",
    "MollomG70", "NowForever", "SiemensV20", "DanfossVLT2800",
]

KINEMATICS_TYPE_NAMES = ["Cartesian", "CoreXY", "midtbot", "parallel_delta", "WallPlotter"]

MOTOR_DRIVER_TYPE_NAMES = [
    "standard_stepper", "stepstick", "tmc_2130", "tmc_2208", "tmc_5160",
    "tmc_2209", "tmc_5160Pro", "tmc_2160Pro", "tmc_2160", "rc_servo",
    "solenoid", "dynamixel2", "null_motor",
]

EXTENDER_TYPE_NAMES = ["pca9539", "pca9535_9555"]  # extenders: is deprecated but still parseable

# field-name -> allowed canonical enum values, matched wherever that field
# name appears as a leaf string value anywhere in the document
ENUM_FIELDS = {
    "engine": ["Timed", "RMT", "I2S_STATIC", "I2S_STREAM"],
    "run_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "homing_mode": ["StealthChop", "CoolStep", "StallGuard"],
    "message_level": ["None", "Error", "Warn", "Info", "Debug", "Verbose"],
    "axis": ["x", "y", "z", "a", "b", "c"],  # parking.axis
}


def _canonical_key_match(key: str, canonical_names: list[str]):
    """Return the canonical name if key case-insensitively matches one of
    canonical_names but isn't already an exact match; else None."""
    if key in canonical_names:
        return None  # already canonical, nothing to normalize
    for name in canonical_names:
        if key.lower() == name.lower():
            return name
    return None


def _canonical_value_match(value, canonical_values: list[str]):
    if not isinstance(value, str) or value in canonical_values:
        return None
    for v in canonical_values:
        if value.lower() == v.lower():
            return v
    return None


def normalize_permissive(doc):
    """
    Walk a parsed FluidNC config document, normalizing known
    case-insensitive identifiers to canonical casing. Returns
    (normalized_doc, warnings) -- the input is not mutated.

    warnings is a list of {"path": [...], "message": "..."} dicts, same
    shape as jsonschema errors, so callers can present them uniformly.
    """
    doc = copy.deepcopy(doc)
    warnings = []

    def warn(path, message):
        warnings.append({"path": list(path), "message": message})

    def walk_enum_fields(node, path):
        if isinstance(node, dict):
            for k, v in list(node.items()):
                if k in ENUM_FIELDS and isinstance(v, str):
                    canon = _canonical_value_match(v, ENUM_FIELDS[k])
                    if canon:
                        warn(path + [k], f"'{v}' normalized to canonical casing '{canon}' (case-insensitive match; real firmware accepts either)")
                        node[k] = canon
                walk_enum_fields(v, path + [k])
        elif isinstance(node, list):
            for i, item in enumerate(node):
                walk_enum_fields(item, path + [i])

    def rename_matching_key(node, path, canonical_names, context_label):
        """If node is a dict with exactly one key matching canonical_names
        case-insensitively, rename it to canonical casing in place."""
        if not isinstance(node, dict):
            return
        for k in list(node.keys()):
            canon = _canonical_key_match(k, canonical_names)
            if canon:
                warn(path + [k], f"{context_label} key '{k}' normalized to canonical casing '{canon}' (case-insensitive match; real firmware accepts either)")
                node[canon] = node.pop(k)

    # 1. Top-level spindle/VFD type names
    rename_matching_key(doc, [], SPINDLE_TYPE_NAMES, "spindle type")

    # 2. kinematics: <type> nested key
    if isinstance(doc.get("kinematics"), dict):
        rename_matching_key(doc["kinematics"], ["kinematics"], KINEMATICS_TYPE_NAMES, "kinematics type")

    # 3. axes.<letter>.motor0/motor1 driver-type nested key
    if isinstance(doc.get("axes"), dict):
        for axis_letter, axis_block in doc["axes"].items():
            if not isinstance(axis_block, dict):
                continue
            for motor_key in ("motor0", "motor1"):
                motor_block = axis_block.get(motor_key)
                if isinstance(motor_block, dict):
                    rename_matching_key(
                        motor_block, ["axes", axis_letter, motor_key],
                        MOTOR_DRIVER_TYPE_NAMES, "motor driver type",
                    )

    # 4. extenders.pinextenderN.<type> nested key (deprecated feature, still normalized for completeness)
    if isinstance(doc.get("extenders"), dict):
        for pe_key, pe_block in doc["extenders"].items():
            if isinstance(pe_block, dict):
                rename_matching_key(pe_block, ["extenders", pe_key], EXTENDER_TYPE_NAMES, "extender type")

    # 5. Enum-valued leaf fields, anywhere in the document
    walk_enum_fields(doc, [])

    return doc, warnings


def load_schema(schema_path: Path) -> dict:
    with open(schema_path) as f:
        return json.load(f)


def validate_document(doc, schema: dict, permissive: bool = False) -> dict:
    """
    Validate a parsed FluidNC config document against schema.

    Returns {"valid": bool, "errors": [...], "warnings": [...]}.
    In strict mode (permissive=False), warnings is always [].
    In permissive mode, known case-insensitive identifiers are normalized
    before validation and reported as warnings instead of errors.
    """
    if Draft202012Validator is None:
        raise RuntimeError("jsonschema package is not installed")

    warnings = []
    if permissive:
        doc, warnings = normalize_permissive(doc if doc is not None else {})
    elif doc is None:
        doc = {}

    validator = Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(doc), key=lambda e: list(e.absolute_path))
    return {
        "valid": len(errors) == 0,
        "errors": [{"path": list(e.absolute_path), "message": e.message} for e in errors],
        "warnings": warnings,
    }
