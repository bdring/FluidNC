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
import functools
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
DEFAULT_FOR_TAG_RE = re.compile(r'^\s*//\s*@default_for\s+(\S+)\s*$')
DEFAULT_TAG_RE = re.compile(r'^\s*//\s*@default\s+(.+?)\s*$')
DEFAULT_NOTE_TAG_RE = re.compile(r'^\s*//\s*@default_note\s+(.+?)\s*$')
TUNING_TAG_RE = re.compile(r'^\s*//\s*@tuning\s+(\S+)\s*$')
PIN_ATTRIBUTES_TAG_RE = re.compile(r'^\s*//\s*@pin_attributes\s+(\S+)\s*$')
# Same shape/purpose as @default_for (see ItemDocs.md): a subclass overriding
# a shared base class's item -- but for @pin_attributes instead of @default.
# Needed because a handful of fields (e.g. OnOffSpindle's output_pin: plain
# digital Output for OnOff/Relay/DAC, PWM for PWM/Laser/10V/BESC, set in each
# concrete type's own init()) have one @config block shared by every
# contributor but a REAL pin_attributes value that genuinely differs by
# concrete type.
PIN_ATTRIBUTES_FOR_TAG_RE = re.compile(r'^\s*//\s*@pin_attributes_for\s+(\S+)\s*$')
UNIT_TAG_RE = re.compile(r'^\s*//\s*@unit\s+(.+?)\s*$')
# @ignore_drift <reason> -- see ItemDocs.md. Deliberately requires a reason
# (never a bare flag): a silent, unexplained suppression is exactly the kind
# of warning-that-nobody-reads this exists to prevent. IGNORE_DRIFT_BARE_RE
# catches the no-reason form so it's a hard parse error, not a silently
# swallowed line that quietly becomes part of the field's description.
IGNORE_DRIFT_TAG_RE = re.compile(r'^\s*//\s*@ignore_drift\s+(.+?)\s*$')
IGNORE_DRIFT_BARE_RE = re.compile(r'^\s*//\s*@ignore_drift\s*$')
COMMENT_LINE_RE = re.compile(r'^\s*//(.*)$')

# The only two values @tuning may take -- see ItemDocs.md. Anything else is
# almost certainly a typo (e.g. "per_machine" instead of "per-machine") and
# should fail loudly rather than silently pass through as an unrecognized
# string a downstream consumer might mishandle.
VALID_TUNING_VALUES = {"typical", "per-machine"}

# @pin_attributes' allowed values -- see ItemDocs.md. A single bare value,
# not composable (e.g. never "output spi") -- a bus-membership value
# (spi/i2s/uart/i2c) stands alone for a field that IS one of that bus's own
# signal lines (native peripheral pin-matrix assignment, no Pin::setAttr()
# direction call in source at all -- e.g. spi.mosi_pin), while a
# direction/capability value applies to an ordinary GPIO-style field that
# goes through the wizard's generic board-pin role selection instead. adc/dac
# are reserved (no current handler.item() field uses them).
VALID_PIN_ATTRIBUTES = {
    "input", "output", "io", "pwm", "isr", "adc", "dac",
    "spi", "i2s", "uart", "i2c",
}

# Reserved @default value meaning "no fixed literal exists" (board-dependent,
# code-substituted, ...) -- see ItemDocs.md. Distinct from None/missing,
# which means the annotation is simply absent (an error, see missing_default).
NO_LITERAL_DEFAULT = "(none)"

# Stable suffix every @default-vs-initializer drift warning ends with (see
# build_entry()'s own drift-check below) -- a shared constant, not a string
# literal duplicated at each of the two sites that need it (here and
# build_config_docs.py's --fail-on-drift check), so they can't quietly drift
# apart from each other.
DRIFT_WARNING_SUFFIX = "-- check for drift"


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
    """Return (docs, missing_default, bad_tuning, bad_pin_attributes,
    default_for_overrides, missing_default_for, pin_attributes_for_overrides,
    bad_pin_attributes_for, missing_pin_attributes_for, bad_ignore_drift)
    from @config, @default_for, and @pin_attributes_for blocks.

    docs: {item_name: {"default": str|None, "default_note": str|None, "ignore_drift": str|None, "tuning": str|None, "pin_attributes": str|None, "unit": str|None, "description": str}}
    missing_default: names of @config blocks that had no immediately-following
    @default line -- a required field per ItemDocs.md, reported as an error
    by the caller rather than silently treated as "no default."
    bad_tuning: (name, value) pairs where @tuning's value wasn't one of
    VALID_TUNING_VALUES -- reported as an error, same reasoning as
    missing_default (a typo here should fail loudly, not pass through).
    bad_pin_attributes: (name, value) pairs where @pin_attributes' value
    wasn't one of VALID_PIN_ATTRIBUTES -- same treatment as bad_tuning.
    Whether the tag was used on a non-pin-typed item is a separate check
    the caller (build_entry()/process_class()) makes once the item's kind
    is known, since this function only sees raw comment text.
    default_for_overrides: {item_name: {"default": str, "default_note": str|None}}
    from @default_for blocks (see ItemDocs.md) -- a subclass overriding an
    inherited item's effective default, NOT a new item declaration. Applied
    by the caller (build_config_docs.py's merge_section()) as a final pass
    over the fully-merged section, not here -- this function only extracts
    what a single class's own body says.
    missing_default_for: names of @default_for blocks missing their required
    @default line, same treatment as missing_default.
    pin_attributes_for_overrides: {item_name: pin_attributes_str} from
    @pin_attributes_for blocks -- same shape/purpose as default_for_overrides,
    for @pin_attributes instead of @default (e.g. OnOffSpindle's shared
    output_pin: plain Output for OnOff/Relay/DAC, PWM for PWM/Laser/10V/BESC).
    bad_pin_attributes_for/missing_pin_attributes_for: same treatment as
    bad_pin_attributes/missing_default_for.
    bad_ignore_drift: names of @config blocks with a bare @ignore_drift tag
    (no reason text) -- reported as an error, same reasoning as missing_default
    -- see @ignore_drift's own tag-declaration comment.
    """
    docs = {}
    missing_default = []
    bad_tuning = []
    bad_pin_attributes = []
    default_for_overrides = {}
    missing_default_for = []
    pin_attributes_for_overrides = {}
    bad_pin_attributes_for = []
    missing_pin_attributes_for = []
    bad_ignore_drift = []
    lines = body.splitlines()
    i = 0
    while i < len(lines):
        dfm = DEFAULT_FOR_TAG_RE.match(lines[i])
        if dfm:
            for_name = dfm.group(1)
            i += 1
            for_default = None
            if i < len(lines):
                fdm = DEFAULT_TAG_RE.match(lines[i])
                if fdm:
                    for_default = fdm.group(1)
                    i += 1
            if for_default is None:
                missing_default_for.append(for_name)
                continue
            for_default_note = None
            if i < len(lines):
                fdnm = DEFAULT_NOTE_TAG_RE.match(lines[i])
                if fdnm:
                    for_default_note = fdnm.group(1)
                    i += 1
            default_for_overrides[for_name] = {"default": for_default, "default_note": for_default_note}
            continue
        pafm = PIN_ATTRIBUTES_FOR_TAG_RE.match(lines[i])
        if pafm:
            for_name = pafm.group(1)
            i += 1
            for_pin_attributes = None
            if i < len(lines):
                pam = PIN_ATTRIBUTES_TAG_RE.match(lines[i])
                if pam:
                    for_pin_attributes = pam.group(1)
                    i += 1
            if for_pin_attributes is None:
                missing_pin_attributes_for.append(for_name)
                continue
            if for_pin_attributes not in VALID_PIN_ATTRIBUTES:
                bad_pin_attributes_for.append((for_name, for_pin_attributes))
                continue
            pin_attributes_for_overrides[for_name] = for_pin_attributes
            continue
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
        ignore_drift = None
        if i < len(lines):
            idm = IGNORE_DRIFT_TAG_RE.match(lines[i])
            if idm:
                ignore_drift = idm.group(1)
                i += 1
            elif IGNORE_DRIFT_BARE_RE.match(lines[i]):
                bad_ignore_drift.append(name)
                i += 1
        tuning = None
        if i < len(lines):
            tm = TUNING_TAG_RE.match(lines[i])
            if tm:
                tuning = tm.group(1)
                i += 1
                if tuning not in VALID_TUNING_VALUES:
                    bad_tuning.append((name, tuning))
        pin_attributes = None
        if i < len(lines):
            pam = PIN_ATTRIBUTES_TAG_RE.match(lines[i])
            if pam:
                pin_attributes = pam.group(1)
                i += 1
                if pin_attributes not in VALID_PIN_ATTRIBUTES:
                    bad_pin_attributes.append((name, pin_attributes))
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
            "ignore_drift": ignore_drift,
            "tuning": tuning,
            "pin_attributes": pin_attributes,
            "unit": unit,
            "description": "\n\n".join(paragraphs),
        }
    return (
        docs, missing_default, bad_tuning, bad_pin_attributes, default_for_overrides, missing_default_for,
        pin_attributes_for_overrides, bad_pin_attributes_for, missing_pin_attributes_for, bad_ignore_drift,
    )


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


def _find_member_in_text(text: str, member_re: str, array_suffix: str):
    """In-class declaration/initializer search, factored out of
    find_member_decl() so the same two patterns (with and without an
    initializer) can be tried against an ancestor class's own file too, not
    just the class being processed."""
    m = re.search(
        rf'^\s*(?:static\s+)?([\w:<>]+[\s*&]*)\s+{member_re}{array_suffix}\s*=\s*([^;]+);',
        text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), m.group(2).strip()
    m = re.search(
        rf'^\s*(?:static\s+)?([\w:<>]+[\s*&]*)\s+{member_re}{array_suffix}\s*;',
        text,
        re.MULTILINE,
    )
    if m:
        return normalize_type(m.group(1)), None
    return None, None


def find_member_decl(cpp_text: str, header_text: str, class_name: str, member: str, class_hierarchy: dict = None):
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
    # In-class declaration/initializer in this class's own header.
    found = _find_member_in_text(header_text, member_re, ARRAY_SUFFIX)
    if found != (None, None):
        return found
    # Not declared in this class's own file -- walk the real C++ inheritance
    # chain (class_hierarchy, from scan_class_hierarchy()) up to wherever the
    # member actually lives. A subclass's group() commonly calls
    # handler.item() on a member it never redeclares, only inherits -- e.g.
    # Laser::group() sets _pwm_freq, declared in PWMSpindle.h (Laser's
    # parent); Solenoid::group() reaches two levels up into RcServo.h. Only
    # in-class declarations are searched at each ancestor (never a
    # ClassName::member static out-of-line form -- an inherited instance
    # member is never declared that way).
    if class_hierarchy is not None:
        current, seen = class_name, set()
        while True:
            entry = class_hierarchy.get(current)
            if not entry:
                break
            parent_file, parent_class = entry
            if parent_class in seen:
                break
            seen.add(parent_class)
            found = _find_member_in_text(parent_file.read_text(), member_re, ARRAY_SUFFIX)
            if found != (None, None):
                return found
            current = parent_class
    return None, None


def normalize_type(raw: str) -> str:
    t = raw.strip()
    t = re.sub(r'\s+', ' ', t)
    if t.endswith('*') and not t.endswith('* '):
        t = t.replace(' *', '*').replace('*', '*')
    return t.rstrip('&').strip()


def _number_lists_match(cpp_literal: str, doc_default: str) -> bool:
    """Array-vs-array counterpart of the plain numeric-equality check above --
    a C++ brace-init list (e.g. "{ 0.0, 0.0, 0.0 }") and @default's own
    whitespace-separated Float Array syntax (e.g. "0.0 0.0 0.0", per
    ItemDocs.md/the config spec's own grammar) are never going to look alike
    as plain substrings, even when every element is identical -- the braces
    and commas are pure C++ syntax with no config.yaml meaning at all. Strips
    them from both sides, splits on whitespace, then compares element-by-
    element (numerically where both sides parse as numbers, so "0.50" still
    matches "0.5" per-element too). Returns False (not a match) for anything
    that isn't actually list-shaped -- a single bare scalar has no "{"/","
    to strip, so this can only ever add matches on top of the plain
    substring/numeric checks, never take one away.
    """
    strip_chars = str.maketrans('', '', '{},')
    cpp_tokens = cpp_literal.translate(strip_chars).split()
    doc_tokens = doc_default.translate(strip_chars).split()
    if len(cpp_tokens) < 2 or len(cpp_tokens) != len(doc_tokens):
        return False
    for cpp_tok, doc_tok in zip(cpp_tokens, doc_tokens):
        if cpp_tok == doc_tok:
            continue
        try:
            if float(cpp_tok) == float(doc_tok):
                continue
        except ValueError:
            pass
        return False
    return True


def build_entry(call, cpp_text, header_text, class_name, docs, enum_lookup, enum_arrays, warnings, class_hierarchy=None):
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
        cpp_type, default_literal = find_member_decl(cpp_text, header_text, class_name, base_member, class_hierarchy)

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
        # call is unambiguously an enum. enum_arrays is tree-wide (scan_enum_arrays_tree()),
        # so this resolves regardless of which file actually declares the array (e.g.
        # message_level's array lives in Logging.cpp, not wherever message_level itself
        # is a config item).
        entry["kind"] = "enum"
        if extra[0] in enum_arrays:
            entry["values"] = enum_arrays[extra[0]]["values"]
            # The array's own C++ name -- e.g. "trinamicModes" -- carried
            # alongside the resolved values so build_config_docs.py's final
            # document assembly can define each distinct enum type ONCE (as a
            # YAML anchor) and have every other usage site reference it via
            # alias, instead of re-expanding the same list at every field
            # that happens to share this array (see to_yaml()'s enum_registry
            # param). Purely a rendering concern -- doesn't affect what a
            # parsed config_items.yaml looks like in memory.
            entry["values_name"] = extra[0]

    if kind == "enum" and member in enum_lookup:
        # step_engine_t*-typed fields (e.g. Stepping::engine) don't pass their EnumItem
        # array as an item() argument at all -- the array is only linked by convention
        # (see main()'s enum_lookup construction), not by anything visible at this call site.
        entry["values"] = enum_lookup[member]["values"]
        entry["values_name"] = enum_lookup[member].get("name")

    doc = docs.get(name)
    if doc:
        entry["default"] = doc["default"]
        if doc.get("default_note"):
            entry["default_note"] = doc["default_note"]
        if doc.get("tuning"):
            entry["tuning"] = doc["tuning"]
        if doc.get("pin_attributes"):
            entry["pin_attributes"] = doc["pin_attributes"]
        if doc["unit"]:
            entry["unit"] = doc["unit"]
        entry["description"] = doc["description"]

        if default_literal is not None and doc["default"] is not None and doc["default"] != NO_LITERAL_DEFAULT and not doc.get("ignore_drift"):
            # Best-effort cross-check: the annotated text should at least
            # mention the literal the initializer shows (e.g. "255" appearing
            # somewhere in "255 -- special ..."). A miss doesn't necessarily
            # mean the annotation is wrong (some defaults are legitimately
            # code-determined, not a plain literal), so this is a warning,
            # not a hard failure. Strip a trailing C++ float-literal suffix
            # (0.002f -> 0.002) first -- otherwise every plain float default
            # would spuriously warn, since the annotation naturally omits it.
            # Skipped entirely when @default is the (none) sentinel -- see
            # ItemDocs.md -- there's deliberately no literal to compare -- or
            # when @ignore_drift is present: a human has already looked at
            # this exact mismatch and confirmed it's not real drift (e.g. a
            # symbolic C++ constant this script has no way to evaluate), so
            # skip re-flagging it forever rather than training reviewers to
            # scroll past a warning they can never actually clear.
            literal_for_compare = re.sub(r'(?<=[0-9])[fFlL]$', '', default_literal)
            if literal_for_compare not in doc["default"]:
                # Substring match failed -- before warning, also try a numeric
                # match (e.g. "0.50" in the initializer vs "0.5" in @default,
                # or "133.50" vs "133.5"): same value, just a different
                # trailing-zero rendering between C++ and the hand-written
                # annotation, not real drift. Only kicks in when BOTH sides
                # actually parse as plain numbers -- a symbolic constant
                # (Z_AXIS, SERVO_PWM_FREQ_DEFAULT, UartData::Bits8, a
                # brace-init array, ...) still can't be resolved this way and
                # correctly falls through to a real warning.
                numeric_match = False
                try:
                    numeric_match = float(literal_for_compare) == float(doc["default"])
                except ValueError:
                    pass
                if not numeric_match and _number_lists_match(literal_for_compare, doc["default"]):
                    numeric_match = True
                if not numeric_match:
                    warnings.append(
                        f'"{name}": @default says {doc["default"]!r} but the field '
                        f'initializer literal is {default_literal!r} {DRIFT_WARNING_SUFFIX}'
                    )
    else:
        entry["default"] = None

    return name, entry


def find_enum_arrays(cpp_text: str):
    """Special-case: EnumItem-style arrays like `stepTypes[] = { {X, "Name"}, ... };`,
    including the out-of-class-static form `const EnumItem EthPhy::phyTypes[] = {...}`
    (the `(?:\\w+::)?` prefix strips the class qualifier, since call sites always
    reference the array unqualified from within the class's own scope, e.g.
    `handler.item("phy_type", _phy_type, phyTypes)`).

    Returns {array_name: {"values": [...]}} -- only the choice list. The
    default is deliberately NOT extracted here even though the array often
    encodes one (e.g. `EnumItem(DEFAULT_STEPPING_ENGINE)`); per ItemDocs.md,
    the `@default` annotation is the sole source of truth for defaults, since
    this kind of computed/board-dependent value is exactly the case a script
    can't safely resolve on its own (it's a macro, not a literal).
    """
    results = {}
    for m in re.finditer(r'EnumItem\s+(?:\w+::)?(\w+)\s*\[\]\s*=\s*\{(.*?)\};', cpp_text, re.DOTALL):
        array_name, body = m.group(1), m.group(2)
        values = re.findall(r'"([^"]+)"', body)
        results[array_name] = {"values": values}
    return results


def find_src_root(cpp_file: Path) -> Path:
    """Walk up from cpp_file to the ancestor directory literally named 'src'
    (FluidNC/src/, the root every SECTIONS entry in build_config_docs.py is
    relative to) -- used to scope the tree-wide scans below."""
    for p in [cpp_file] + list(cpp_file.parents):
        if p.name == "src":
            return p
    raise SystemExit(f"error: could not locate a 'src' directory above {cpp_file}")


@functools.lru_cache(maxsize=None)
def scan_enum_arrays_tree(src_root: Path) -> dict:
    """find_enum_arrays(), applied to every .cpp/.h file under src_root and
    merged. Only 5 EnumItem arrays exist in the whole codebase today (a plain
    `grep -rn 'EnumItem.*\\[\\]\\s*=' FluidNC/src` confirms it), each declared
    once -- a whole-tree scan is cheap (a few hundred small files) and, unlike
    a fixed file list, never needs updating when a 6th one is added somewhere
    new. Cached per src_root since process_class() is called once per
    (file, class) contributor -- dozens of times per full docs build -- and
    the tree doesn't change mid-run."""
    results = {}
    for p in sorted(src_root.rglob("*")):
        if p.suffix in (".cpp", ".h"):
            results.update(find_enum_arrays(p.read_text()))
    return results


CLASS_DECL_RE = re.compile(r'^\s*class\s+(\w+)\s*:\s*public\s+(?:\w+::)?(\w+)', re.MULTILINE)


@functools.lru_cache(maxsize=None)
def scan_class_hierarchy(src_root: Path) -> dict:
    """{class_name: (file_path, parent_class_name)} for every `class X : public Y`
    (or `class X : public Y, Z` -- only the first base is captured, which is
    always the config-relevant one for this codebase's occasional
    Configuration::Configurable mixin, e.g. ModbusVFD's
    `public VFDProtocol, Configuration::Configurable`) declaration under
    src_root, scanning both .h and .cpp (some VFD subclasses like Huanyang
    are declared directly in a .cpp with no header at all). Cached per
    src_root, same reasoning as scan_enum_arrays_tree()."""
    result = {}
    for p in sorted(src_root.rglob("*")):
        if p.suffix not in (".h", ".cpp"):
            continue
        text = p.read_text()
        for m in CLASS_DECL_RE.finditer(text):
            result[m.group(1)] = (p, m.group(2))
    return result


def to_yaml(section: str, entries: dict, enum_registry: dict = None) -> str:
    """enum_registry: optional {values_name: anchor_name} for enum types
    already defined earlier in the document (see build_config_docs.py's
    enum_types preamble) -- an entry whose values came from a name in this
    map gets a YAML alias (`values: *anchor_name`) instead of re-expanding
    the list inline. A parsed document looks identical either way (YAML
    aliases resolve to the same value on load); this only changes what the
    file looks like on disk. None (the default, and what this script's own
    single-class CLI passes) keeps today's always-inline behavior."""
    lines = [f"{section}:"]
    for name, e in entries.items():
        lines.append(f"  {name}:")
        lines.append(f"    type: {e['kind']}")
        if "min" in e:
            lines.append(f"    min: {e['min']}")
        if "max" in e:
            lines.append(f"    max: {e['max']}")
        if "values" in e:
            values_name = e.get("values_name")
            anchor = enum_registry.get(values_name) if enum_registry else None
            if anchor:
                lines.append(f"    values: *{anchor}")
            else:
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
        if "pin_attributes" in e:
            lines.append(f"    pin_attributes: {e['pin_attributes']}")
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
    Returns (entries, errors, warnings, default_for_overrides,
    pin_attributes_for_overrides).

    Reusable core shared by this script's own CLI (one file/class at a time)
    and tools/build_config_docs.py (which calls this once per contributing
    class/method and merges results into one flattened config-section --
    needed because a few spindle types split their items across group() and
    a shared groupCommon() helper, e.g. OnOff/PlasmaSpindle).

    default_for_overrides (see ItemDocs.md's @default_for) is applied to
    THIS class's own `entries` immediately below when the name it targets
    happens to already be declared in this same body (a same-class
    override, not the common case) -- but is also returned as-is so
    build_config_docs.py's merge_section() can apply it CROSS-class, after
    every contributor to a section has been merged (the actual point of
    @default_for: a subclass overriding an item a shared base class
    declared in a different file entirely). pin_attributes_for_overrides
    (@pin_attributes_for) works identically, one level over for
    @pin_attributes instead of @default.
    """
    cpp_text = cpp_file.read_text()
    header_path = cpp_file.with_suffix(".h")
    header_text = header_path.read_text() if header_path.exists() else ""

    src_root = find_src_root(cpp_file)
    enum_arrays = scan_enum_arrays_tree(src_root)
    class_hierarchy = scan_class_hierarchy(src_root)

    body = find_group_body(cpp_text, class_name, method_name)
    (
        docs, missing_default, bad_tuning, bad_pin_attributes, default_for_overrides, missing_default_for,
        pin_attributes_for_overrides, bad_pin_attributes_for, missing_pin_attributes_for, bad_ignore_drift,
    ) = parse_doc_blocks(body)
    calls = parse_item_calls(body)

    # Heuristic link: a step_engine*-typed item's enum choices come from
    # whichever EnumItem array holds its named choices. For Stepping
    # specifically this is `stepTypes` -- look it up by name (not "take
    # whichever array comes first", now that enum_arrays is tree-wide and
    # holds all 5) -- expose it by member name so build_entry() can find it
    # regardless of the array's own name.
    enum_lookup = {}
    for call in calls:
        cpp_type, _ = find_member_decl(cpp_text, header_text, class_name, call["member"] or "", class_hierarchy)
        if cpp_type == "step_engine_t*" and "stepTypes" in enum_arrays:
            enum_lookup[call["member"]] = {"values": enum_arrays["stepTypes"]["values"], "name": "stepTypes"}

    warnings = []
    entries = {}
    for call in calls:
        name, entry = build_entry(call, cpp_text, header_text, class_name, docs, enum_lookup, enum_arrays, warnings, class_hierarchy)
        entries[name] = entry

    # Same-class @default_for/@pin_attributes_for application -- see this
    # function's own docstring. Harmless no-op when (as usual) the target
    # name isn't declared in this same body; the cross-class case is
    # handled by the caller using the returned overrides dicts.
    for for_name, override in default_for_overrides.items():
        if for_name in entries:
            entries[for_name]["default"] = override["default"]
            if override["default_note"]:
                entries[for_name]["default_note"] = override["default_note"]
    for for_name, pin_attributes in pin_attributes_for_overrides.items():
        if for_name in entries:
            entries[for_name]["pin_attributes"] = pin_attributes

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
    for name in bad_ignore_drift:
        errors.append(f'@config {name} has a bare @ignore_drift with no reason text -- see ItemDocs.md')
    for name, value in bad_tuning:
        errors.append(f'@config {name} has @tuning {value!r} -- must be one of {sorted(VALID_TUNING_VALUES)}')
    for name, value in bad_pin_attributes:
        errors.append(f'@config {name} has @pin_attributes {value!r} -- must be one of {sorted(VALID_PIN_ATTRIBUTES)}')
    # @pin_attributes only makes sense on a type: pin item -- anywhere else
    # it's certainly a copy-paste mistake, not a real annotation.
    for name, doc in docs.items():
        if doc.get("pin_attributes") and name in entries and entries[name]["kind"] != "pin":
            errors.append(f'@config {name} has @pin_attributes but is type {entries[name]["kind"]!r}, not "pin"')
    for name in missing_default_for:
        errors.append(f'@default_for {name} is missing its required @default line')
    for name, value in bad_pin_attributes_for:
        errors.append(f'@pin_attributes_for {name} has @pin_attributes {value!r} -- must be one of {sorted(VALID_PIN_ATTRIBUTES)}')
    for name in missing_pin_attributes_for:
        errors.append(f'@pin_attributes_for {name} is missing its required @pin_attributes line')
    # Same non-pin-type sanity check as @pin_attributes above, but for a
    # same-class @pin_attributes_for target (the cross-class case is
    # checked by build_config_docs.py's merge_section() instead, once the
    # full section is merged).
    for name in pin_attributes_for_overrides:
        if name in entries and entries[name]["kind"] != "pin":
            errors.append(f'@pin_attributes_for {name} but is type {entries[name]["kind"]!r}, not "pin"')

    return entries, errors, warnings, default_for_overrides, pin_attributes_for_overrides


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("cpp_file", type=Path)
    ap.add_argument("--class", dest="class_name", required=True)
    ap.add_argument("--section", required=True)
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()

    # default_for_overrides/pin_attributes_for_overrides are unused here -- a
    # single-class run has no OTHER contributor to apply a cross-class
    # override to; only build_config_docs.py's merge_section()
    # (multi-contributor) needs them.
    entries, errors, warnings, _default_for_overrides, _pin_attributes_for_overrides = process_class(args.cpp_file, args.class_name)

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
