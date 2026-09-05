#!/usr/bin/env python3
"""
fluidnc_config_mcp_server.py — MCP server exposing FluidNC config.yaml
validation (against fluidnc-config-schema.json) as tools any MCP-capable
LLM client can call directly.

Setup:
    python3 -m venv venv
    ./venv/bin/pip install "mcp[cli]" pyyaml jsonschema

    Place fluidnc-config-schema.json AND fluidnc_validate_core.py in the
    SAME DIRECTORY as this script (or set FLUIDNC_SCHEMA_PATH to point the
    schema elsewhere).

Run standalone (stdio transport):
    ./venv/bin/python3 fluidnc_config_mcp_server.py

--- Claude Desktop ---
Add to claude_desktop_config.json:
    {
      "mcpServers": {
        "fluidnc-config-validator": {
          "command": "/absolute/path/to/venv/bin/python3",
          "args": ["/absolute/path/to/fluidnc_config_mcp_server.py"]
        }
      }
    }

--- Claude Code ---
    claude mcp add fluidnc-config-validator -- /absolute/path/to/venv/bin/python3 /absolute/path/to/fluidnc_config_mcp_server.py

--- Any other MCP stdio client ---
Point it at the same command + args shown above.

This server exposes two tools:
    validate_fluidnc_config(yaml_text, permissive=False)   — validate config content passed as a string
    validate_fluidnc_config_file(path, permissive=False)   — validate an existing file on disk

Both return {"valid": bool, "errors": [...], "warnings": [...]}
(or {"valid": false, "yaml_parse_error": "..."} if the YAML itself doesn't parse).

Strict vs permissive: FluidNC's real parser matches most identifiers
case-insensitively (e.g. 'pwm'/'PWM' are equivalent to it). By default
(permissive=False) this is treated strictly, and casing mismatches are
reported as errors. Pass permissive=True to normalize known
case-insensitive identifiers before validating -- normalizations are then
reported as non-blocking "warnings" instead. See fluidnc_validate_core.py
for exactly which identifiers this covers.

Separately, deprecated-feature usage (e.g. the extenders: section,
pinext-syntax pin values) is always reported as a "warnings" entry
regardless of permissive -- "warnings" is not empty only in permissive
mode; a strictly-valid-but-deprecated config will have warnings too.

Scope, mirrored from fluidnc-config-schema.json's own description: this checks
structural/type/range/enum correctness only. It does NOT check YAML-syntax-level
rules (indentation consistency, tabs, etc. — see the companion markdown spec §0)
beyond what's needed to parse the YAML at all, and does NOT check board-specific
pin legality (which GPIO numbers exist on a given board) — both intentionally
out of scope.
"""
import os
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("Missing dependency: pyyaml. Install with:\n"
          "  pip install pyyaml", file=sys.stderr)
    sys.exit(1)

try:
    import jsonschema  # noqa: F401  (imported for the early, clear error message)
except ImportError:
    print("Missing dependency: jsonschema. Install with:\n"
          "  pip install jsonschema", file=sys.stderr)
    sys.exit(1)

try:
    from mcp.server.fastmcp import FastMCP
except ImportError:
    print("Missing dependency: mcp. Install with:\n"
          '  pip install "mcp[cli]"', file=sys.stderr)
    sys.exit(1)

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from fluidnc_validate_core import load_schema, validate_document
except ImportError:
    print("Missing file: fluidnc_validate_core.py must be in the same directory as this script.",
          file=sys.stderr)
    sys.exit(1)

SCHEMA_PATH = Path(os.environ.get(
    "FLUIDNC_SCHEMA_PATH",
    Path(__file__).resolve().parent / "fluidnc-config-schema.json",
))

_schema: dict | None = None


def _get_schema() -> dict:
    global _schema
    if _schema is None:
        if not SCHEMA_PATH.exists():
            raise FileNotFoundError(
                f"fluidnc-config-schema.json not found at {SCHEMA_PATH}. "
                "Place it next to this script, or set FLUIDNC_SCHEMA_PATH."
            )
        _schema = load_schema(SCHEMA_PATH)
    return _schema


mcp = FastMCP("fluidnc-config-validator")


@mcp.tool()
def validate_fluidnc_config(yaml_text: str, permissive: bool = False) -> dict:
    """
    Validate FluidNC config.yaml content against the FluidNC config JSON Schema.

    Pass the full text of a config.yaml file as a string (e.g. content you just
    generated). Returns:
        {"valid": true, "errors": [], "warnings": []}                      on success, nothing deprecated used
        {"valid": true, "errors": [], "warnings": [...]}                   on success, but using a deprecated feature -- check "warnings"
        {"valid": false, "errors": [{"path":[...],"message":"..."}], "warnings":[...]}  on schema violations
        {"valid": false, "yaml_parse_error": "..."}                        if the text isn't valid YAML

    Call this after generating or editing a FluidNC config, and fix any
    reported errors before presenting the result to the user. Also check
    "warnings" even when valid is true -- deprecated-feature usage (e.g.
    the extenders: section, pinext-syntax pins) is reported there
    regardless of the permissive setting below.

    permissive: FluidNC's real parser matches most identifiers (spindle
    types, motor driver types, kinematics types, several enum values)
    case-insensitively. Default (False) treats casing mismatches as hard
    errors -- appropriate when generating new content, since one consistent
    canonical form is more useful to standardize on than to leave ambiguous.
    Set True when validating existing/human-written configs where casing
    style shouldn't block acceptance -- normalizations are then reported as
    non-blocking "warnings" instead of "errors".

    This checks structural/type/range/enum correctness only -- it does not
    check board-specific pin legality (which GPIO numbers exist on a given
    board), which is intentionally out of scope.
    """
    try:
        doc = yaml.safe_load(yaml_text)
    except yaml.YAMLError as e:
        return {"valid": False, "yaml_parse_error": str(e)}
    return validate_document(doc, _get_schema(), permissive=permissive)


@mcp.tool()
def validate_fluidnc_config_file(path: str, permissive: bool = False) -> dict:
    """
    Validate an existing FluidNC config.yaml FILE ON DISK against the FluidNC
    config JSON Schema. Use this instead of validate_fluidnc_config when the
    config already exists as a file (e.g. after writing it to the filesystem)
    rather than being held as a string.

    See validate_fluidnc_config for the meaning of `permissive`.

    Returns the same shape as validate_fluidnc_config, plus "path": the
    resolved path that was checked.
    """
    p = Path(path)
    if not p.exists():
        return {"valid": False, "error": f"file not found: {path}"}
    try:
        with open(p) as f:
            doc = yaml.safe_load(f)
    except yaml.YAMLError as e:
        return {"valid": False, "yaml_parse_error": str(e), "path": str(p)}
    result = validate_document(doc, _get_schema(), permissive=permissive)
    result["path"] = str(p)
    return result


if __name__ == "__main__":
    mcp.run()
