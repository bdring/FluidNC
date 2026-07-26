// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "Serial.h"                 // Cmd
#include "System.h"                 // sys
#include "Machine/MachineConfig.h"  // config
#include "Job.h"                    // Job::

void MacroEvent::run(void* arg) const {
    config->_macros->_macro[_num].run(nullptr);
}

const MacroEvent macro0Event { 0 };
const MacroEvent macro1Event { 1 };
const MacroEvent macro2Event { 2 };
const MacroEvent macro3Event { 3 };

// Macros::group() (Macros.h) registers each of these via handler.item(macro.name(), macro),
// not a literal handler.item("name", ...) call per macro -- see ItemDocs.md's "data-driven
// item lists" section for why these are annotated here, at construction, instead. Every
// field here is a String (one config-file line, "&"-separated sub-commands, default empty
// meaning "do nothing").

// @config startup_line0
// @default "" (empty)
// Legacy Grbl feature (formerly $N0). Runs once, the first time the firmware enters Idle
// after boot.
Macro Macros::_startup_line0 { "startup_line0" };

// @config startup_line1
// @default "" (empty)
// Legacy Grbl feature (formerly $N1). Runs once, the first time the firmware enters Idle
// after boot, immediately after startup_line0.
Macro Macros::_startup_line1 { "startup_line1" };

Macro Macros::_macro[] = {
    // @config macro0
    // @default "" (empty)
    // Runs when macro0_pin (control:) is activated. The switch must read inactive at
    // startup -- deactivate it before clearing the alarm, same as any other control input.
    Macro { "Macro0" },
    // @config macro1
    // @default "" (empty)
    // Runs when macro1_pin (control:) is activated.
    Macro { "Macro1" },
    // @config macro2
    // @default "" (empty)
    // Runs when macro2_pin (control:) is activated.
    Macro { "Macro2" },
    // @config macro3
    // @default "" (empty)
    // Runs when macro3_pin (control:) is activated.
    Macro { "Macro3" },
};

// @config after_homing
// @default "" (empty)
// Runs after a homing cycle completes -- i.e. once every axis with homing enabled has been
// homed.
Macro Macros::_after_homing { "after_homing" };

// @config after_reset
// @default "" (empty)
// Runs after the system resets (power-on/startup, or a Ctrl-X real-time reset), but only if
// the system ends up in Idle state immediately after the reset.
Macro Macros::_after_reset { "after_reset" };

// @config after_unlock
// @default "" (empty)
// Runs after a $X unlock command.
Macro Macros::_after_unlock { "after_unlock" };

// clang-format off
const std::map<std::string, Cmd> overrideCodes = {
    { "fr", Cmd::FeedOvrReset },
    { "f>", Cmd::FeedOvrCoarsePlus },
    { "f<", Cmd::FeedOvrCoarseMinus },
    { "f+", Cmd::FeedOvrFinePlus },
    { "f-", Cmd::FeedOvrFineMinus },
    { "rr", Cmd::RapidOvrReset },
    { "rm", Cmd::RapidOvrMedium },
    { "rl", Cmd::RapidOvrLow },
    { "rx", Cmd::RapidOvrExtraLow },
    { "sr", Cmd::SpindleOvrReset },
    { "s>", Cmd::SpindleOvrCoarsePlus },
    { "s<", Cmd::SpindleOvrCoarseMinus },
    { "s+", Cmd::SpindleOvrFinePlus },
    { "s-", Cmd::SpindleOvrFineMinus },
    { "ss", Cmd::SpindleOvrStop },
    { "ft", Cmd::CoolantFloodOvrToggle },
    { "mt", Cmd::CoolantMistOvrToggle },
};
// clang-format on

Cmd findOverride(std::string name) {
    auto it = overrideCodes.find(name);
    return it == overrideCodes.end() ? Cmd::None : it->second;
}

bool Macro::run(Channel* channel) {
    if (_gcode.length()) {
        if (channel) {
            log_debug_to(*channel, "Run " << name() << ": " << _gcode);
        }
        Job::save();
        Job::nest(new MacroChannel(this), channel);
        return true;
    }
    return false;
}

Error MacroChannel::readLine(char* line, size_t maxlen) {
    size_t             len       = 0;
    const std::string& gcode     = _macro->_gcode;
    const int          gcode_len = gcode.length();
    while (_position < gcode_len) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        char c = gcode[_position++];
        // XXX this can probably be pushed into the GCode parser alongside expressions
        // Realtime characters can be inserted in macros with #xx escapes
        if (c == '#') {
            if ((_position + 2) <= gcode_len) {
                Cmd cmd = findOverride(gcode.substr(_position, 2));
                if (cmd != Cmd::None) {
                    _position += 2;
                    execute_realtime_command(cmd, *this);
                    continue;
                }
            }
        }
        // & is a proxy for newlines in macros, because you cannot
        // enter a newline directly in a config file string value.
        if (c == '&' || c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    ++_line_number;
    if (len == 0) {
        ++_blank_lines;
    }

    return len ? Error::Ok : Error::Eof;
}

void MacroChannel::ack(Error status) {
    if (status != Error::Ok) {
        //        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << name() << " at line " << lineNumber());
        //        if (status != Error::GcodeUnsupportedCommand) {
        // Do not stop on unsupported commands because most senders do not stop.
        // Stop the macro job on other errors
        notifyf("Macro job error", "Error:%d in %s at line: %d", status, name(), lineNumber());
        _pending_error = status;
        //        }
    }
}

MacroChannel::MacroChannel(Macro* macro) : Channel(macro->name(), false), _macro(macro) {}

void MacroChannel::end_message() {
    _progress += name();
    _progress += ": Sent";
}

Error MacroChannel::pollLine(char* line) {
    // Macros only execute as proper jobs so we should not be polling one with a null line
    if (!line) {
        return Error::NoData;
    }
    if (_pending_error != Error::Ok) {
        return _pending_error;
    }
    if (_percent) {
        _percent = false;
        // If the first non-blank line in the macro is a % line, it denotes start-of-file.
        // Otherwise a % line causes the rest of the macro to be skipped, per
        // https://linuxcnc.org/docs/html/gcode/overview.html#gcode:file-requirements
        // The line with % is not blank, so if it is the first non-blank line
        // _line_number will be one more than _blank_lines
        if (_line_number != _blank_lines + 1) {
            _ended = true;
        }
    }
    if (_ended) {
        end_message();
        return Error::Eof;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            log_debug("Macro line: " << line);
            float percent_complete = (float)_position * 100.0f / _macro->get().length();

            _progress = "SD:" + formatFloat(percent_complete, 2) + "," + name();
        }
            return Error::Ok;
        case Error::Eof:
            end_message();
            return Error::Eof;
        default:
            log_error("Macro readLine failed");
            _progress = "";
            return err;
    }
}

MacroChannel::~MacroChannel() {}
