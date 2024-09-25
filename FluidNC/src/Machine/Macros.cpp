// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "src/Serial.h"                 // Cmd
#include "src/System.h"                 // sys
#include "src/Machine/MachineConfig.h"  // config
#include "src/Job.h"                    // Job::
#include <sstream>
#include <iomanip>

void MacroEvent::run(void* arg) const {
    config->_macros->_macro[_num].run(nullptr);
}

const MacroEvent macro0Event { 0 };
const MacroEvent macro1Event { 1 };
const MacroEvent macro2Event { 2 };
const MacroEvent macro3Event { 3 };

Macro Macros::_startup_line0 { "startup_line0" };
Macro Macros::_startup_line1 { "startup_line1" };
Macro Macros::_macro[] = {
    Macro { "Macro0" },
    Macro { "Macro1" },
    Macro { "Macro2" },
    Macro { "Macro3" },
};

Macro Macros::_after_homing { "after_homing" };
Macro Macros::_after_reset { "after_reset" };
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

Error MacroChannel::readLine(char* line, int maxlen) {
    int                len       = 0;
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
    return len ? Error::Ok : Error::Eof;
}

void MacroChannel::ack(Error status) {
    if (status != Error::Ok) {
        //        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << name() << " at line " << lineNumber());
        //        if (status != Error::GcodeUnsupportedCommand) {
        // Do not stop on unsupported commands because most senders do not stop.
        // Stop the macro job on other errors
        notifyf("Macro job error", "Error:%d in %s at line: %d", status, name().c_str(), lineNumber());
        _pending_error = status;
        //        }
    }
}

MacroChannel::MacroChannel(Macro* macro) : Channel(macro->name(), false), _macro(macro) {}

Error MacroChannel::pollLine(char* line) {
    // Macros only execute as proper jobs so we should not be polling one with a null line
    if (!line) {
        return Error::NoData;
    }
    if (_pending_error != Error::Ok) {
        return _pending_error;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            log_debug("Macro line: " << line);
            float percent_complete = (float)_position * 100.0f / _macro->get().length();

            std::ostringstream s;
            s << "SD:" << std::fixed << std::setprecision(2) << percent_complete << "," << name();
            _progress = s.str();
        }
            return Error::Ok;
        case Error::Eof:
            _progress = name();
            _progress += ": Sent";
            return Error::Eof;
        default:
            log_error("Macro readLine failed");
            _progress = "";
            return err;
    }
}

MacroChannel::~MacroChannel() {}
