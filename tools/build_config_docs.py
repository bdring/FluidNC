#!/usr/bin/env python3
"""Build one combined config-item doc file from every annotated class in the tree.

Walks the SECTIONS table below -- each entry is a real (or templated, for
per-axis/per-motor/per-driver repeats) config.yaml path, plus the ordered list
of (file, class[, method]) contributors whose fields flatten into it, mirroring
C++ inheritance (base class first) and the groupCommon()-style helper split a
few spindle types use. Emits FluidNC/docs/config_items.yaml by default, or
wherever --output points (build-release.py points it at the release folder).

This is the "single artifact" the annotation effort (see ItemDocs.md) was
building toward: one machine-readable file a config wizard can load for
hoverable tooltips, and a human can read as a from-source spec.

Repo root is assumed to be the current working directory.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import gen_config_docs as g  # noqa: E402

SRC = Path("FluidNC/src")

# Each section: (config_path, [(relative_file, class_name[, method_name]), ...], note_or_None)
# method_name defaults to "group" when omitted.
SECTIONS = [
    ("(top-level machine items)", [("Machine/MachineConfig.cpp", "MachineConfig")], None),
    ("start", [("Machine/MachineConfig.h", "Start")], None),
    ("stepping", [("Stepping.cpp", "Stepping")], None),
    ("coolant", [("CoolantControl.cpp", "CoolantControl")], None),
    ("probe", [("Probe.cpp", "Probe")], None),
    ("parking", [("Parking.cpp", "Parking")], None),
    ("spi", [("Machine/SPIBus.cpp", "SPIBus")], None),
    ("i2cN", [("Machine/I2CBus.cpp", "I2CBus")], None),
    ("i2so", [("Machine/I2SOBus.cpp", "I2SOBus")], None),
    ("sdcard", [("SDCard.h", "SDCard")], None),
    ("user_outputs", [("Machine/UserOutputs.cpp", "UserOutputs")], None),
    ("uartN", [("Uart.cpp", "Uart")], None),
    ("uart_channelN", [("UartChannel.h", "UartChannel")], None),
    ("status_outputs", [("Status_outputs.h", "Status_Outputs")], None),
    ("ethernet", [("Machine/EthPhy.h", "EthPhy")], None),
    (
        "oled",
        [("../esp32/OLED.h", "OLED")],
        "Lives under FluidNC/esp32/ (an ESP32-specific module), not FluidNC/src/ like every "
        "other section here -- the relative path above deliberately escapes SRC to reach it.",
    ),
    ("atc_manual", [("ToolChangers/atc_manual.h", "Manual_ATC")], None),
    (
        "extenders.pinextenderN.<i2c_chip>",
        [("Extenders/I2CPinExtenderBase.cpp", "I2CPinExtenderBase")],
        "PROVISIONAL -- pin extenders may be removed in a future FluidNC version.",
    ),
    ("axes", [("Machine/Axes.cpp", "Axes")], "Group-level keys, siblings of the axis letters."),
    ("axes.<letter>", [("Machine/Axis.cpp", "Axis")], None),
    ("axes.<letter>.homing", [("Machine/Homing.h", "Homing")], None),
    ("axes.<letter>.motorN", [("Machine/Motor.cpp", "Motor")], "Fields shared by every driver type, siblings of the driver-type block below."),
    ("axes.<letter>.motorN.standard_stepper", [("Motors/StandardStepper.h", "StandardStepper")], None),
    (
        "axes.<letter>.motorN.stepstick",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/StepStick.cpp", "StepStick")],
        None,
    ),
    (
        "axes.<letter>.motorN.tmc_2130",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TrinamicBase.h", "TrinamicBase"), ("Motors/TrinamicSpiDriver.h", "TrinamicSpiDriver")],
        "TMC2130Driver.cpp registers this name but adds no fields of its own -- identical field set to the shared SPI-driver base.",
    ),
    (
        "axes.<letter>.motorN.tmc_2208",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TrinamicBase.h", "TrinamicBase"), ("Motors/TrinamicUartDriver.h", "TrinamicUartDriver"), ("Motors/TMC2208Driver.h", "TMC2208Driver")],
        "Not individually addressable over UART -- in a daisy chain, the LAST-defined motor's shared-field values win at runtime.",
    ),
    (
        "axes.<letter>.motorN.tmc_2209",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TrinamicBase.h", "TrinamicBase"), ("Motors/TrinamicUartDriver.h", "TrinamicUartDriver"), ("Motors/TMC2209Driver.h", "TMC2209Driver")],
        None,
    ),
    (
        "axes.<letter>.motorN.tmc_5160",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TrinamicBase.h", "TrinamicBase"), ("Motors/TrinamicSpiDriver.h", "TrinamicSpiDriver"), ("Motors/TMC5160Driver.h", "TMC5160Driver")],
        "Also registered as tmc_2160 (own SECTIONS entry just below, sharing this contributor list) --"
        " confirmed via TMC2160Driver.h's own class (extends TMC5160Driver, this same ordinary"
        " TrinamicSpiDriver-based type) and TMC2160Driver.cpp's registration(\"tmc_2160\") call. NOT"
        " the same driver as tmc_2160Pro below, despite the similar name -- that one is a raw-register"
        " tmc_5160Pro alias instead (a DIFFERENT class, confusingly also named TMC2160Driver, declared"
        " in TMC2160ProDriver.h) -- verify against the actual registration() call before trusting a"
        " name-similarity assumption here again.",
    ),
    (
        "axes.<letter>.motorN.tmc_2160",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TrinamicBase.h", "TrinamicBase"), ("Motors/TrinamicSpiDriver.h", "TrinamicSpiDriver"), ("Motors/TMC5160Driver.h", "TMC5160Driver")],
        "Registration alias of tmc_5160 -- see that entry's own note (and do not confuse with"
        " tmc_2160Pro, a different, raw-register alias of tmc_5160Pro instead).",
    ),
    (
        "axes.<letter>.motorN.tmc_5160Pro",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TMC5160ProDriver.h", "TMC5160ProDriver")],
        "Also registered as tmc_2160Pro (own SECTIONS entry just below, sharing this contributor"
        " list) -- confirmed via TMC2160ProDriver.h's own class (extends TMC5160ProDriver, this same"
        " raw-register-expert-mode type) and TMC2160ProDriver.cpp's registration(\"tmc_2160Pro\") call."
        " Bypasses TrinamicBase/TrinamicSpiDriver entirely, unlike every other tmc_* type -- no"
        " run_amps/microsteps/etc. here. An EARLIER version of this note incorrectly also claimed"
        " tmc_2160 (no \"Pro\") as a third alias of this same raw-register class -- it is not; see"
        " tmc_5160's own entry above.",
    ),
    (
        "axes.<letter>.motorN.tmc_2160Pro",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TMC5160ProDriver.h", "TMC5160ProDriver")],
        "Registration alias of tmc_5160Pro -- see that entry's own note.",
    ),
    ("axes.<letter>.motorN.rc_servo", [("Motors/RcServo.h", "RcServo")], None),
    ("axes.<letter>.motorN.solenoid", [("Motors/Solenoid.h", "Solenoid")], None),
    ("axes.<letter>.motorN.dynamixel2", [("Motors/Dynamixel2.h", "Dynamixel2")], None),
    ("axes.<letter>.motorN.null_motor", [], "Explicit no-op driver -- takes no fields."),
    (
        "PWM",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
            ("Spindles/PWMSpindle.h", "PWM"),
        ],
        None,
    ),
    (
        "Laser",
        [("Spindles/Spindle.h", "Spindle"), ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"), ("Spindles/LaserSpindle.h", "Laser")],
        "Skips direction_pin (Laser calls OnOff::groupCommon() directly, not OnOff::group()) and never calls "
        "Spindle::groupDelaySettings() -- so unlike every other spindle type, spinup_ms/spindown_ms are genuinely absent here, "
        "not merely present-but-inert.",
    ),
    (
        "10V",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
            ("Spindles/PWMSpindle.h", "PWM"),
            ("Spindles/10vSpindle.h", "_10v"),
        ],
        None,
    ),
    (
        "BESC",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
            ("Spindles/PWMSpindle.h", "PWM"),
            ("Spindles/BESCSpindle.h", "BESC"),
        ],
        None,
    ),
    (
        "HBridge",
        [("Spindles/Spindle.h", "Spindle"), ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"), ("Spindles/HBridgeSpindle.h", "HBridge")],
        None,
    ),
    (
        "OnOff",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
        ],
        "A base class FluidNC itself never instantiates directly -- \"Relay\" and \"DAC\" below are the real, "
        "selectable registrations, each with their own (field-identical, but not necessarily "
        "default-identical -- see speed_map) section.",
    ),
    (
        "Relay",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
            ("Spindles/RelaySpindle.h", "Relay"),
        ],
        "Adds no fields of its own -- same field set as OnOff, listed as its own section (rather than folded "
        "into OnOff's) because it's a real, separately-registered, user-selectable spindle type.",
    ),
    (
        "DAC",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/OnOffSpindle.h", "OnOff", "group"),
            ("Spindles/OnOffSpindle.h", "OnOff", "groupCommon"),
            ("Spindles/DacSpindle.h", "Dac"),
        ],
        "Adds no fields of its own -- same field set as OnOff/Relay, listed as its own section for the same "
        "reason as Relay. Its speed_map default genuinely differs from OnOff/Relay's, though (see @default_for "
        "in DacSpindle.h) -- unlike Relay, DacSpindle.cpp's own init() installs a different curve.",
    ),
    (
        "PlasmaSpindle",
        [("Spindles/Spindle.h", "Spindle"), ("Spindles/PlasmaSpindle.h", "PlasmaSpindle", "groupCommon")],
        "Never calls Spindle::groupDelaySettings() -- spinup_ms/spindown_ms are genuinely absent here, not merely present-but-inert.",
    ),
    ("NoSpindle", [], 'The "Null" spindle -- takes no fields, no I/O.'),
    (
        "ModbusVFD",
        [
            ("Spindles/Spindle.h", "Spindle"),
            ("Spindles/Spindle.h", "Spindle", "groupDelaySettings"),
            ("Spindles/VFDSpindle.cpp", "VFDSpindle"),
            ("Spindles/VFD/ModbusVFD.h", "ModbusVFD"),
        ],
        "Also backs Huanyang/YL620/H2A/SiemensV20/NowForever/FolinnBD600/DeltaMS300/H100/MollomG70/DanfossVLT2800 -- each registers under its "
        "own name with fixed constructor-supplied command strings, adding no fields of its own. spinup_ms/spindown_ms are always present here "
        "(VFDSpindle::group() always calls groupDelaySettings()), but only actually applied at runtime when get_rpm_cmd is left unset -- see "
        "get_rpm_cmd's own description for why.",
    ),
    ("kinematics.ParallelDelta", [("Kinematics/ParallelDelta.cpp", "ParallelDelta")], None),
    ("kinematics.WallPlotter", [("Kinematics/WallPlotter.cpp", "WallPlotter")], None),
    ("kinematics.CoreXY", [("Kinematics/CoreXY.cpp", "CoreXY")], None),
    (
        "kinematics.midtbot / kinematics.Cartesian",
        [],
        "No config items at all -- Midtbot::group()/Cartesian::group() are empty. Midtbot hardcodes "
        "its x_scaler to 2.0 in init() rather than exposing it as a config item, since that ratio is "
        "a fixed property of the midTbot hardware design, not something a user should tune.",
    ),
]

# "Data-driven list" files (see ItemDocs.md): the item's real name comes from a
# runtime .legend()/.name() call, not a string literal passed to handler.item(),
# so gen_config_docs's normal item()-call parser can't see these at all. Their
# @config/@default/description blocks are still real and correct (written by
# hand against the actual registration code) -- extract them directly by
# scanning for @config blocks anywhere in the file, and supply the type by hand
# since there's no item() call for build_entry() to infer it from.
LIST_MODE_SECTIONS = [
    ("control", "Control.cpp", "pin"),
    ("user_inputs", "Machine/UserInputs.cpp", "pin"),
    ("macros", "Machine/Macros.cpp", "macro"),
    ("rgbled", "Listeners/RGBLed.h", None),  # mixed types, see RGBLED_TYPES below
]
RGBLED_TYPES = {"pin": "pin", "index": "integer"}  # everything else in that file is a color string
LIST_MODE_NOTES = {
    "control": "Registered via a runtime loop over a list of (event, name, letter) tuples, not literal handler.item() calls -- "
    "see ItemDocs.md's \"data-driven item lists\" section. Extracted here from the @config annotations directly.",
    "user_inputs": "Same data-driven-list shape as control: above.",
    "macros": "Same data-driven-list shape as control: above. All fields are Strings (one config-file line, \"&\"-separated).",
    "rgbled": "PROVISIONAL / DO NOT USE -- the Listeners/SysListener framework this depends on is likely to be removed. "
    "Same data-driven-list shape as control: above, one level more indirect (routed through a handleRGBString() wrapper).",
}


def merge_section(contributors):
    entries = {}
    all_errors = []
    all_warnings = []
    # Collected across every contributor, applied only after the full entries
    # merge below -- @default_for (see ItemDocs.md) exists specifically so a
    # subclass can override an item a DIFFERENT, earlier-merged contributor
    # declared (e.g. PWMSpindle.h overriding speed_map's default, which
    # Spindle.h itself declares) -- applying per-contributor inline, instead
    # of as a final pass, would make the result depend on contributor order
    # for no reason.
    default_for_overrides = {}
    # Same idea, for @pin_attributes_for (see ItemDocs.md) -- e.g.
    # PWMSpindle.h overriding output_pin's pin_attributes, which
    # OnOffSpindle.h itself declares as the shared/common value.
    pin_attributes_for_overrides = {}
    for contrib in contributors:
        rel_file, class_name = contrib[0], contrib[1]
        method_name = contrib[2] if len(contrib) > 2 else "group"
        e, errors, warnings, overrides, pa_overrides = g.process_class(SRC / rel_file, class_name, method_name)
        entries.update(e)
        default_for_overrides.update(overrides)
        pin_attributes_for_overrides.update(pa_overrides)
        all_errors.extend(f"{rel_file}::{class_name}.{method_name}: {msg}" for msg in errors)
        all_warnings.extend(f"{rel_file}::{class_name}.{method_name}: {msg}" for msg in warnings)
    for name, override in default_for_overrides.items():
        if name not in entries:
            all_warnings.append(f'@default_for {name} does not match any item in this section\'s merged field set')
            continue
        entries[name]["default"] = override["default"]
        if override["default_note"]:
            entries[name]["default_note"] = override["default_note"]
    for name, pin_attributes in pin_attributes_for_overrides.items():
        if name not in entries:
            all_warnings.append(f'@pin_attributes_for {name} does not match any item in this section\'s merged field set')
            continue
        if entries[name]["kind"] != "pin":
            all_errors.append(f'@pin_attributes_for {name} but is type {entries[name]["kind"]!r}, not "pin"')
            continue
        entries[name]["pin_attributes"] = pin_attributes
    return entries, all_errors, all_warnings


def list_mode_section(rel_file, kind_for=None):
    text = (SRC / rel_file).read_text()
    # default_for_overrides/missing_default_for are unused here -- @default_for
    # (see ItemDocs.md) exists for a subclass overriding a shared base
    # class's item, which doesn't arise in this data-driven-list shape.
    # pin_attributes_for/bad_pin_attributes_for/missing_pin_attributes_for are
    # unused here -- @pin_attributes_for (like @default_for) exists for a
    # subclass overriding a shared base class's item, which doesn't arise in
    # this data-driven-list shape (same reasoning as default_for_overrides
    # just above).
    # bad_ignore_drift is unused here too -- @ignore_drift (see ItemDocs.md)
    # exists to suppress the @default-vs-initializer drift check, which never
    # runs in this data-driven-list shape at all (there's no single
    # handler.item() call to find a field initializer next to).
    (
        docs, missing_default, bad_tuning, bad_pin_attributes, _default_for_overrides, _missing_default_for,
        _pin_attributes_for_overrides, _bad_pin_attributes_for, _missing_pin_attributes_for, _bad_ignore_drift,
    ) = g.parse_doc_blocks(text)
    entries = {}
    errors = []
    bad_tuning_names = {name for name, _ in bad_tuning}
    bad_pin_attributes_names = {name for name, _ in bad_pin_attributes}
    for name, doc in docs.items():
        if name in missing_default:
            errors.append(f"@config {name} is missing its required @default line")
            continue
        if name in bad_tuning_names:
            value = next(v for n, v in bad_tuning if n == name)
            errors.append(f"@config {name} has @tuning {value!r} -- must be one of {sorted(g.VALID_TUNING_VALUES)}")
            continue
        if name in bad_pin_attributes_names:
            value = next(v for n, v in bad_pin_attributes if n == name)
            errors.append(f"@config {name} has @pin_attributes {value!r} -- must be one of {sorted(g.VALID_PIN_ATTRIBUTES)}")
            continue
        kind = "string"
        if kind_for == "pin":
            kind = "pin"
        elif kind_for == "macro":
            kind = "macro"
        elif kind_for is None:  # RGBLed: mixed
            kind = RGBLED_TYPES.get(name, "string (hex color RRGGBB, or \"none\")")
        if doc.get("pin_attributes") and kind != "pin":
            errors.append(f'@config {name} has @pin_attributes but is type {kind!r}, not "pin"')
            continue
        entry = {"kind": kind, "default": doc["default"]}
        if doc.get("default_note"):
            entry["default_note"] = doc["default_note"]
        if doc.get("tuning"):
            entry["tuning"] = doc["tuning"]
        if doc.get("pin_attributes"):
            entry["pin_attributes"] = doc["pin_attributes"]
        if doc["unit"]:
            entry["unit"] = doc["unit"]
        entry["description"] = doc["description"]
        entries[name] = entry
    return entries, errors, []


def main():
    import argparse

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "-o", "--output", type=Path, default=Path("FluidNC/docs/config_items.yaml"),
        help="Output path (default: FluidNC/docs/config_items.yaml). "
        "build-release.py points this at release/current/docs/config_items.yaml.",
    )
    args = ap.parse_args()

    # Pass 1: compute every section's entries first (not renders yet) --
    # needed before rendering starts so the enum_types preamble below can
    # see every entry's values_name across the WHOLE document, not just one
    # section at a time.
    any_errors = False
    section_results = []  # (section, entries_or_None, note)
    for section, contributors, note in SECTIONS:
        if not contributors:
            section_results.append((section, None, note))
            continue
        entries, errors, warnings = merge_section(contributors)
        if errors:
            any_errors = True
            for e in errors:
                print(f"error [{section}]: {e}", file=sys.stderr)
            continue
        for w in warnings:
            print(f"warning [{section}]: {w}", file=sys.stderr)
        section_results.append((section, entries, note))

    list_mode_results = []  # (section, entries)
    for section, rel_file, kind_for in LIST_MODE_SECTIONS:
        entries, errors, _ = list_mode_section(rel_file, kind_for)
        if errors:
            any_errors = True
            for e in errors:
                print(f"error [{section}]: {e}", file=sys.stderr)
            continue
        list_mode_results.append((section, entries))

    if any_errors:
        raise SystemExit(1)

    # Pass 2: collect every distinct enum type actually used (keyed by the
    # C++ array's own name, e.g. "trinamicModes") across every section, and
    # render it ONCE as a YAML anchor in a preamble block -- every usage site
    # then aliases it (see to_yaml()'s enum_registry param) instead of
    # re-expanding the same values list at each of its, sometimes many,
    # usage sites (run_mode/homing_mode alone repeat trinamicModes 4 times).
    # A parsed document is identical either way -- YAML aliases resolve to
    # the same value on load -- this only de-duplicates the file on disk.
    enum_types = {}  # values_name -> values list
    for _, entries, _ in section_results:
        if not entries:
            continue
        for e in entries.values():
            vn = e.get("values_name")
            if vn and vn not in enum_types:
                enum_types[vn] = e["values"]
    for _, entries in list_mode_results:
        for e in entries.values():
            vn = e.get("values_name")
            if vn and vn not in enum_types:
                enum_types[vn] = e["values"]
    enum_registry = {name: name for name in enum_types}  # anchor name == array name

    out_lines = [
        "# FluidNC config item reference -- GENERATED FILE, DO NOT EDIT BY HAND.",
        "#",
        "# Produced by tools/build_config_docs.py from the // @config / @default",
        "# annotations living next to each handler.item(...) call in FluidNC/src.",
        "# See FluidNC/src/Configuration/ItemDocs.md for the annotation convention,",
        "# and CONFIG_ITEM_ISSUES.md for known wiki-vs-source drift this supersedes.",
        "#",
        "# Regenerate with: python3 tools/build_config_docs.py",
        "",
    ]

    if enum_types:
        out_lines.append(
            "# Enum value sets, named after the C++ EnumItem array they come from --"
        )
        out_lines.append(
            "# defined once here and referenced via YAML alias (*name) at each field"
        )
        out_lines.append("# below that uses one, instead of repeating the list inline every time.")
        out_lines.append("enum_types:")
        for name in sorted(enum_types):
            vals = ", ".join(enum_types[name])
            out_lines.append(f"  {name}: &{name} [{vals}]")
        out_lines.append("")

    for section, entries, note in section_results:
        if entries is None:
            # A real, single config.yaml key (no " / " or parens -- those
            # mark a joined/descriptive label covering more than one actual
            # section, e.g. "kinematics.midtbot / kinematics.Cartesian" or
            # "(top-level machine items)", which can't become one real YAML
            # key) with zero real config items (e.g. null_motor/NoSpindle,
            # explicit no-op types with an empty group()) still gets emitted
            # as a real, empty top-level section -- not just a comment --
            # so a consumer walking config_items.yaml's own top-level key
            # list (e.g. the wizard deriving which motor/spindle type names
            # are real) sees it exist, the same as every other real type.
            if note:
                out_lines.append(f"# {note}")
            if " / " not in section and not section.startswith("("):
                out_lines.append(g.to_yaml(section, {}, enum_registry))
            else:
                out_lines.append(f"# {section}: (no config items)")
                out_lines.append("")
            continue
        if note:
            out_lines.append(f"# {note}")
        out_lines.append(g.to_yaml(section, entries, enum_registry))

    for section, entries in list_mode_results:
        out_lines.append(f"# {LIST_MODE_NOTES[section]}")
        out_lines.append(g.to_yaml(section, entries, enum_registry))

    out_path = args.output
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out_lines))
    print(f"Wrote {out_path} ({sum(1 for l in out_lines if l.strip())} non-blank lines)")


if __name__ == "__main__":
    main()
