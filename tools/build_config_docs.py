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
        None,
    ),
    (
        "axes.<letter>.motorN.tmc_5160Pro",
        [("Motors/StandardStepper.h", "StandardStepper"), ("Motors/TMC5160ProDriver.h", "TMC5160ProDriver")],
        "Also registered as tmc_2160Pro and tmc_2160 -- all three names are the exact same driver class/fields (raw-register expert mode). "
        "Bypasses TrinamicBase/TrinamicSpiDriver entirely, unlike every other tmc_* type -- no run_amps/microsteps/etc. here.",
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
        'Also backs the "Relay" and "DAC" registrations (RelaySpindle.cpp/DacSpindle.cpp), which add no fields of their own -- same field set.',
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
    for contrib in contributors:
        rel_file, class_name = contrib[0], contrib[1]
        method_name = contrib[2] if len(contrib) > 2 else "group"
        e, errors, warnings = g.process_class(SRC / rel_file, class_name, method_name)
        entries.update(e)
        all_errors.extend(f"{rel_file}::{class_name}.{method_name}: {msg}" for msg in errors)
        all_warnings.extend(f"{rel_file}::{class_name}.{method_name}: {msg}" for msg in warnings)
    return entries, all_errors, all_warnings


def list_mode_section(rel_file, kind_for=None):
    text = (SRC / rel_file).read_text()
    docs, missing_default = g.parse_doc_blocks(text)
    entries = {}
    errors = []
    for name, doc in docs.items():
        if name in missing_default:
            errors.append(f"@config {name} is missing its required @default line")
            continue
        kind = "string"
        if kind_for == "pin":
            kind = "pin"
        elif kind_for == "macro":
            kind = "macro"
        elif kind_for is None:  # RGBLed: mixed
            kind = RGBLED_TYPES.get(name, "string (hex color RRGGBB, or \"none\")")
        entry = {"kind": kind, "default": doc["default"]}
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
    any_errors = False
    for section, contributors, note in SECTIONS:
        if not contributors:
            out_lines.append(f"# {section}: (no config items)")
            if note:
                out_lines.append(f"#   {note}")
            out_lines.append("")
            continue
        entries, errors, warnings = merge_section(contributors)
        if errors:
            any_errors = True
            for e in errors:
                print(f"error [{section}]: {e}", file=sys.stderr)
            continue
        for w in warnings:
            print(f"warning [{section}]: {w}", file=sys.stderr)
        if note:
            out_lines.append(f"# {note}")
        out_lines.append(g.to_yaml(section, entries))

    for section, rel_file, kind_for in LIST_MODE_SECTIONS:
        entries, errors, _ = list_mode_section(rel_file, kind_for)
        if errors:
            any_errors = True
            for e in errors:
                print(f"error [{section}]: {e}", file=sys.stderr)
            continue
        out_lines.append(f"# {LIST_MODE_NOTES[section]}")
        out_lines.append(g.to_yaml(section, entries))

    if any_errors:
        raise SystemExit(1)

    out_path = args.output
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(out_lines))
    print(f"Wrote {out_path} ({sum(1 for l in out_lines if l.strip())} non-blank lines)")


if __name__ == "__main__":
    main()
