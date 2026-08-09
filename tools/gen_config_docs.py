#!/usr/bin/env python3
"""Generate a per-section config-item doc summary from annotated FluidNC source.

Pilot script: proves out the `// @config <name>` convention documented in
FluidNC/src/Configuration/ItemDocs.md, starting with the Stepping module.
It is deliberately narrow (single class per invocation, one section name
passed on the command line) rather than a general whole-tree crawler --
broadening it is future work once the convention itself is validated.

Usage:
    python3 tools/gen_config_docs.py FluidNC/src/Stepping.cpp \
        --class Stepping --section stepping -o stepping.doc.yaml

What it does NOT try to do (by design, see ItemDocs.md):
    - Infer type/range/default from anything other than the item() call's
      own arguments and the field's own C++ initializer -- it does not
      evaluate expressions, macros, or run the preprocessor.
    - Resolve which config section a class is mounted under -- the caller
      supplies --section explicitly.
"""
import argparse
import re
import sys
from pathlib import Path

# HandlerBase.h's item() overload table, condensed to (arg-shape) -> kind.
# arg-shape is the number of *value* arguments after (name, member), and
# whether the call passes an EnumItem*-typed argument.
CPP_TYPE_TO_KIND = {
    "bool": "boolean",
    "int32_t": "integer",
    "uint32_t": "integer",
    "uint8_t": "integer",
    "float": "float",
    "std::string": "string",
    "Pin": "pin",
    "InputPin": "pin",
    "EventPin": "pin",
    "IPAddress": "ip_address",
    "Macro": "macro",
    "step_engine_t*": "enum",
    "axis_t": "axis",
}

ITEM_CALL_RE = re.compile(
    r'handler\.item\(\s*"(?P<name>[^"]+)"\s*,\s*(?P<rest>[^;]*)\);', re.DOTALL
)
CONFIG_TAG_RE = re.compile(r'^\s*//\s*@config\s+(\S+)\s*$')
DEFAULT_TAG_RE = re.compile(r'^\s*//\s*@default\s+(.+?)\s*$')
DEFAULT_NOTE_TAG_RE = re.compile(r'^\s*//\s*@default_note\s+(.+?)\s*$')
TUNING_TAG_RE = re.compile(r'^\s*//\s*@tuning\s+(\S+)\s*$')
UNIT_TAG_RE = re.compile(r'^\s*//\s*@unit\s+(.+?)\s*$')
COMMENT_LINE_RE = re.compile(r'^\s*//(.*)$')

# The only two values @tuning may take -- see ItemDocs.md. Anything else is
# almost certainly a typo (e.g. "per_machine" instead of "per-machine") and
# should fail loudly rather than silently pass through as an unrecognized
# string a downstream consumer might mishandle.
VALID_TUNING_VALUES = {"typical", "per-machine"}

# Reserved @default value meaning "no fixed literal exists" (board-dependent,
# code-substituted, ...) -- see ItemDocs.md. Distinct from None/missing,
# which means the annotation is simply absent (an error, see missing_default).
NO_LITERAL_DEFAULT = "(none)"


def find_group_body(cpp_text: str, class_name: str, method_name: str = "group") -> str:
    # Out-of-line definition: void Class::method(Configuration::HandlerBase& h) { ... }
    marker = re.search(
        rf'void\s+{re.escape(class_name)}::{re.escape(method_name)}\s*\(\s*Configuration::HandlerBase\s*&\s*\w+\s*\)\s*\{{',
        cpp_text,
    )
    if not marker:
        # Inline definition inside the class body (common in Motors/*.h, Spindles/*.h,
        # single-class headers like SDCard.h): void method(...) override { ... }, no
        # Class:: qualifier. Only safe to use file-wide when the caller's --class is the
        # one actually meant -- fine for the single-class-per-file convention this repo uses.
        # method_name may be a non-virtual helper (e.g. OnOff::groupCommon()), which never
        # carries "override" -- the trailing (?:override\s*)? already makes that optional.
        marker = re.search(
            rf'void\s+{re.escape(method_name)}\s*\(\s*Configuration::HandlerBase\s*&\s*\w+\s*\)\s*(?:override\s*)?\{{',
            cpp_text,
        )
    if not marker:
        raise SystemExit(f"error: could not find {class_name}::{method_name}(...) in source")
    start = marker.end()
    depth = 1
    i = start
    while depth > 0 and i < len(cpp_text):
        if cpp_text[i] == '{':
            depth += 1
        elif cpp_text[i] == '}':
            depth -= 1
        i += 1
    if depth != 0:
        raise SystemExit(f"error: unmatched braces while scanning {class_name}::{method_name}(...) body")
    return cpp_text[start:i - 1]


def parse_doc_blocks(body: str):
    """Return (docs, missing_default, bad_tuning) from @config blocks.

    docs: {item_name: {"default": str|None, "default_note": str|None, "tuning": str|None, "unit": str|None, "description": str}}
    missing_default: names of @config blocks that had no immediately-following
    @default line -- a required field per ItemDocs.md, reported as an error
    by the caller rather than silently treated as "no default."
    bad_tuning: (name, value) pairs where @tuning's value wasn't one of
    VALID_TUNING_VALUES -- reported as an error, same reasoning as
    missing_default (a typo here should fail loudly, not pass through).
    """
    docs = {}
    missing_default = []
    bad_tuning = []
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        m = CONFIG_TAG_RE.match(lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1)
        i += 1
        default = None
        if i < len(lines):
            dm = DEFAULT_TAG_RE.match(lines[i])
            if dm:
                default = dm.group(1)
                i += 1
        if default is None:
            missing_default.append(name)
        default_note = None
        if i < len(lines):
            dnm = DEFAULT_NOTE_TAG_RE.match(lines[i])
            if dnm:
                default_note = dnm.group(1)
                i += 1
        tuning = None
        if i < len(lines):
            tm = TUNING_TAG_RE.match(lines[i])
            if tm:
                tuning = tm.group(1)
                i += 1
                if tuning not in VALID_TUNING_VALUES:
                    bad_tuning.append((name, tuning))
        unit = None
        if i < len(lines):
            um = UNIT_TAG_RE.match(lines[i])
            if um:
                unit = um.group(1)
                i += 1
        desc_lines = []
        while i < len(lines):
            cm = COMMENT_LINE_RE.match(lines[i])
            if not cm:
                break
            text = cm.group(1).strip()
            desc_lines.append(text)
            i += 1
        # Join: blank comment lines become paragraph breaks.
        paragraphs, current = [], []
        for line in desc_lines:
            if line == "":
                if current:
                    paragraphs.append(" ".join(current))
                    current = []
            else:
                current.append(line)
        if current:
            paragraphs.append(" ".join(current))
        docs[name] = {
            "default": default,
            "default_note": default_note,
            "tuning": tuning,
            "unit": unit,
            "description": "\n\n".join(paragraphs),
        }
    return docs, missing_default, bad_tuning


def parse_item_calls(body: str):
    """Return list of dicts: name, member, extra_args (list of raw arg strings).

    Skips a match whose own line is commented out (e.g. `//handler.item(...)`,
    left as dead reference code) -- ITEM_CALL_RE has no concept of comments, so
    without this a disabled call would be parsed as if it were live.
    """
    calls = []
    for m in ITEM_CALL_RE.finditer(body):
        line_start = body.rfind('\n', 0, m.start()) + 1
        line_prefix = body[line_start:m.start()]
        if line_prefix.lstrip().startswith('//'):
            continue
        name = m.group("name")
        rest = m.group("rest")
        args = split_args(rest)
        member = args[0].strip() if args else None
        extra = [a.strip() for a in args[1:]]
        calls.append({"name": name, "member": member, "extra_args": extra})
    return calls


def split_args(arg_str: str):
    """Split a C++ argument list on top-level commas (ignores commas inside parens)."""
    args, depth, current = [], 0, ""
    for ch in arg_str:
        if ch == ',' and depth == 0:
            args.append(current)
            current = ""
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        current += ch
    if current.strip():
        args.append(current)
    return args


def find_member_decl(cpp_text: str, header_text: str, class_name: str, member: str):
    """Find (cpp_type, default_literal_or_None) for a field, static or in-class.

    `member` is expected already stripped of any array subscript by the caller
    (build_entry) -- ARRAY_SUFFIX below matches the *declaration's own* bracket
    (e.g. `Pin _analogOutput[MaxUserAnalogPin];`), which is a separate, optional
    thing from any subscript on the call-site expression.
    """
    member_re = re.escape(member)
    ARRAY_SUFFIX = r'(?:\[[^\]]*\])?'
    # Static member defined in the .cpp: `Type Class::_member = value;`
    m = re.search(
        rf'^\s*([\w:<>]+[\s*&]*)\s+{re.escape(class_name)}::{member_re}{ARRAY_SUFFIX}\s*=\s*([^;]+);',
        cpp_text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), m.group(2).strip()
    # Static member declared (no initializer) in the .cpp -- default lives elsewhere.
    m = re.search(
        rf'^\s*([\w:<>]+[\s*&]*)\s+{re.escape(class_name)}::{member_re}{ARRAY_SUFFIX}\s*;',
        cpp_text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), None
    # In-class initializer in the header: `Type _member = value;`
    m = re.search(
        rf'^\s*(?:static\s+)?([\w:<>]+[\s*&]*)\s+{member_re}{ARRAY_SUFFIX}\s*=\s*([^;]+);',
        header_text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), m.group(2).strip()
    # Declared with no initializer in the header.
    m = re.search(
        rf'^\s*(?:static\s+)?([\w:<>]+[\s*&]*)\s+{member_re}{ARRAY_SUFFIX}\s*;',
        header_text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), None
    return None, None


def normalize_type(raw: str) -> str:
    t = raw.strip()
    t = re.sub(r'\s+', ' ', t)
    if t.endswith('*') and not t.endswith('* '):
        t = t.replace(' *', '*').replace('*', '*')
    return t.rstrip('&').strip()


def build_entry(call, cpp_text, header_text, class_name, docs, enum_lookup, enum_arrays, warnings):
    """Build one entry. The @default annotation is the authoritative default --
    see ItemDocs.md for why (some real defaults, e.g. board-dependent or
    substituted in afterParse(), aren't discoverable from the initializer at
    all). Whatever literal the field's own initializer shows is used only as
    a best-effort cross-check, surfaced as a warning on an apparent mismatch,
    never as a substitute for the annotation.
    """
    name = call["name"]
    member = call["member"]
    extra = call["extra_args"]
    cpp_type, default_literal = (None, None)
    if member:
        # Strip a trailing array subscript (e.g. "_analogOutput[0]" -> "_analogOutput")
        # before looking up the declaration -- the field is declared as a bare array
        # (`Pin _analogOutput[N];`), never with a per-index name, so the subscripted
        # form never matches. A per-index literal default isn't recoverable this way
        # either (arrays are rarely initialized per-element in the header), so
        # default_literal legitimately comes back None here -- @default carries the
        # real per-index default instead, same as any other code-determined case.
        base_member = re.sub(r'\[[^\]]*\]$', '', member)
        cpp_type, default_literal = find_member_decl(cpp_text, header_text, class_name, base_member)

    if cpp_type in CPP_TYPE_TO_KIND:
        kind = CPP_TYPE_TO_KIND[cpp_type]
    elif cpp_type and cpp_type.endswith("Pin"):
        # A project-specific subclass of Pin/InputPin/EventPin (e.g. ControlPin,
        # Probe::ProbeEventPin) passed to the item(name, EventPin&) overload by
        # upcast -- the declared field type isn't the exact base type HandlerBase
        # overloads on, so it won't be in the table above. Name-suffix heuristic
        # instead of hardcoding every such subclass here as they're added.
        kind = "pin"
    else:
        kind = cpp_type or "unknown"

    entry = {"kind": kind}
    if cpp_type == "UartData":
        # item(name, UartData& wordLength, UartParity& parity, UartStop& stopBits) --
        # HandlerBase's 3-member-reference UART-mode overload. The two "extra" args here
        # are the parity/stopbits member names, not a numeric min/max pair, even though
        # len(extra) == 2 looks identical to the ordinary ranged-numeric shape below.
        entry["kind"] = "uart_mode"
    elif len(extra) == 2:
        entry["min"] = extra[0]
        entry["max"] = extra[1]
    elif len(extra) == 1:
        # item(name, value, SomeEnumArray) -- HandlerBase's item(name, uint32_t&, const
        # EnumItem*) overload. There's exactly one such overload, so any single-extra-arg
        # call is unambiguously an enum even when the array itself is defined in a
        # different file than the one being parsed (e.g. message_level/phy_type) -- only
        # try to resolve the actual choice list when the array happens to be in scope.
        entry["kind"] = "enum"
        if extra[0] in enum_arrays:
            entry["values"] = enum_arrays[extra[0]]["values"]

    if kind == "enum" and member in enum_lookup:
        # step_engine_t*-typed fields (e.g. Stepping::engine) don't pass their EnumItem
        # array as an item() argument at all -- the array is only linked by convention
        # (see main()'s enum_lookup construction), not by anything visible at this call site.
        entry["values"] = enum_lookup[member]["values"]

    doc = docs.get(name)
    if doc:
        entry["default"] = doc["default"]
        if doc.get("default_note"):
            entry["default_note"] = doc["default_note"]
        if doc.get("tuning"):
            entry["tuning"] = doc["tuning"]
        if doc["unit"]:
            entry["unit"] = doc["unit"]
        entry["description"] = doc["description"]

        if default_literal is not None and doc["default"] is not None and doc["default"] != NO_LITERAL_DEFAULT:
            # Best-effort cross-check: the annotated text should at least
            # mention the literal the initializer shows (e.g. "255" appearing
            # somewhere in "255 -- special ..."). A miss doesn't necessarily
            # mean the annotation is wrong (some defaults are legitimately
            # code-determined, not a plain literal), so this is a warning,
            # not a hard failure. Strip a trailing C++ float-literal suffix
            # (0.002f -> 0.002) first -- otherwise every plain float default
            # would spuriously warn, since the annotation naturally omits it.
            # Skipped entirely when @default is the (none) sentinel -- see
            # ItemDocs.md -- there's deliberately no literal to compare.
            literal_for_compare = re.sub(r'(?<=[0-9])[fFlL]$', '', default_literal)
            if literal_for_compare not in doc["default"]:
                warnings.append(
                    f'"{name}": @default says {doc["default"]!r} but the field '
                    f'initializer literal is {default_literal!r} -- check for drift'
                )
    else:
        entry["default"] = None

    return name, entry


def find_enum_arrays(cpp_text: str):
    """Special-case: EnumItem-style arrays like `stepTypes[] = { {X, "Name"}, ... };`.

    Returns {array_name: {"values": [...]}} -- only the choice list. The
    default is deliberately NOT extracted here even though the array often
    encodes one (e.g. `EnumItem(DEFAULT_STEPPING_ENGINE)`); per ItemDocs.md,
    the `@default` annotation is the sole source of truth for defaults, since
    this kind of computed/board-dependent value is exactly the case a script
    can't safely resolve on its own (it's a macro, not a literal).
    """
    results = {}
    for m in re.finditer(r'EnumItem\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\};', cpp_text, re.DOTALL):
        array_name, body = m.group(1), m.group(2)
        values = re.findall(r'"([^"]+)"', body)
        results[array_name] = {"values": values}
    return results


def to_yaml(section: str, entries: dict) -> str:
    lines = [f"{section}:"]
    for name, e in entries.items():
        lines.append(f"  {name}:")
        lines.append(f"    type: {e['kind']}")
        if "min" in e:
            lines.append(f"    min: {e['min']}")
        if "max" in e:
            lines.append(f"    max: {e['max']}")
        if "values" in e:
            vals = ", ".join(e["values"])
            lines.append(f"    values: [{vals}]")
        if e.get("default") is not None:
            lines.append(f"    default: {e['default']!r}")
        else:
            lines.append("    default: null  # missing @default -- should have been caught as an error")
        if "default_note" in e:
            lines.append(f"    default_note: {e['default_note']!r}")
        if "tuning" in e:
            lines.append(f"    tuning: {e['tuning']}")
        if "unit" in e:
            lines.append(f"    unit: {e['unit']!r}")
        if "description" in e:
            desc = e["description"]
            lines.append("    description: |")
            for dl in desc.splitlines():
                lines.append(f"      {dl}" if dl else "")
        lines.append("")
    return "\n".join(lines)


def process_class(cpp_file: Path, class_name: str, method_name: str = "group"):
    """Parse one class's group() (or groupCommon()-style helper) method.
    Returns (entries, errors, warnings).

    Reusable core shared by this script's own CLI (one file/class at a time)
    and tools/build_config_docs.py (which calls this once per contributing
    class/method and merges results into one flattened config-section --
    needed because a few spindle types split their items across group() and
    a shared groupCommon() helper, e.g. OnOff/PlasmaSpindle).
    """
    cpp_text = cpp_file.read_text()
    header_path = cpp_file.with_suffix(".h")
    header_text = header_path.read_text() if header_path.exists() else ""

    body = find_group_body(cpp_text, class_name, method_name)
    docs, missing_default, bad_tuning = parse_doc_blocks(body)
    calls = parse_item_calls(body)
    enum_arrays = find_enum_arrays(cpp_text)

    # Heuristic link: a step_engine*-typed item's enum choices come from
    # whichever EnumItem array holds its named choices. For Stepping
    # specifically this is `stepTypes`; expose it by member name so
    # build_entry() can find it regardless of the array's own name.
    enum_lookup = {}
    for call in calls:
        cpp_type, _ = find_member_decl(cpp_text, header_text, class_name, call["member"] or "")
        if cpp_type == "step_engine_t*" and enum_arrays:
            # Only one such array is expected per pilot module; take the first.
            arr = next(iter(enum_arrays.values()))
            enum_lookup[call["member"]] = arr

    warnings = []
    entries = {}
    for call in calls:
        name, entry = build_entry(call, cpp_text, header_text, class_name, docs, enum_lookup, enum_arrays, warnings)
        entries[name] = entry

    errors = []
    # An @config block whose name never matches a real handler.item() call means
    # the annotation and the code have drifted apart (renamed item, typo, etc.).
    for name in sorted(set(docs) - set(entries)):
        errors.append(f'@config {name} has no matching handler.item("{name}", ...) call')
    # Every item must carry a @config block with @default -- see ItemDocs.md.
    for name in sorted(set(entries) - set(docs)):
        errors.append(f'handler.item("{name}", ...) has no @config/@default annotation')
    for name in missing_default:
        errors.append(f'@config {name} is missing its required @default line')
    for name, value in bad_tuning:
        errors.append(f'@config {name} has @tuning {value!r} -- must be one of {sorted(VALID_TUNING_VALUES)}')

    return entries, errors, warnings


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cpp_file", type=Path)
    ap.add_argument("--class", dest="class_name", required=True)
    ap.add_argument("--section", required=True)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    entries, errors, warnings = process_class(args.cpp_file, args.class_name)

    if errors:
        for e in errors:
            print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)
    for w in warnings:
        print(f"warning: {w}", file=sys.stderr)

    yaml_text = to_yaml(args.section, entries)
    if args.output:
        args.output.write_text(yaml_text)
    else:
        sys.stdout.write(yaml_text)


if __name__ == "__main__":
    main()
