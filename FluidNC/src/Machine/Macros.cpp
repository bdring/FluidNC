// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "src/Serial.h"                 // Cmd
#include "src/System.h"                 // sys
#include "src/Machine/MachineConfig.h"  // config
#include <sstream>
#include <iomanip>

void MacroEvent::run(void* arg) {
    if (sys.state != State::Idle) {
        log_error("Macro can only be used in idle state");
        return;
    }
    log_debug("Macro " << _num);
    config->_macros->run_macro(_num);
}

MacroEvent macro0Event { 0 };
MacroEvent macro1Event { 1 };
MacroEvent macro2Event { 2 };
MacroEvent macro3Event { 3 };

Macro Macros::_startup = { "Startup" };
Macro Macros::_macro[] = {
    { "Macro0" },
    { "Macro1" },
    { "Macro2" },
    { "Macro3" },
};

// clang-format off
std::map<std::string, Cmd> overrideCodes = {
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

Error MacroChannel::readLine(char* line, int maxlen) {
    int  len = 0;
    char c;
    while (_position < _macro->_value.length()) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        char c = _macro->_value[_position++];
        // Realtime characters can be inserted in macros with #xx escapes
        if (c == '#') {
            if ((_position + 2) > _macro->_value.length()) {
                log_error("Missing characters after # realtime escape in macro");
                return Error::Eof;
            }
            {
                std::string s(_macro->_value.c_str() + _position, 2);
                Cmd         cmd = findOverride(s);
                if (cmd == Cmd::None) {
                    log_error("Bad #xx realtime escape in macro");
                    return Error::Eof;
                }
                _position += 2;
                execute_realtime_command(cmd, *this);
            }
        }
        // & is a proxy for newlines in macros, because you cannot
        // enter a newline directly in a config file string value.
        if (c == '&') {
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
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << name() << " at line " << lineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not stop.
            // Stop the macro job on other errors
            _notifyf("Macro job error", "Error:%d in %s at line: %d", status, name().c_str(), lineNumber());
            _pending_error = status;
        }
    }
}

bool Macros::run_startup() {
    if (_startup._value.length()) {
        jobChannels.push(new MacroChannel(&_startup));
        return true;
    }
    return false;
}

bool Macros::run_macro(size_t index) {
    if (index >= n_macros) {
        return false;
    }

    jobChannels.push(new MacroChannel(&_macro[index]));
    return true;
}

MacroChannel::MacroChannel(Macro* macro) : Channel(macro->_name, false), _macro(macro) {}

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
            float percent_complete = (float)_position * 100.0f / _macro->_value.length();

            std::ostringstream s;
            s << name() << ":" << std::fixed << std::setprecision(2) << percent_complete;
            _progress = s.str();
        }
            return Error::Ok;
        case Error::Eof:
            _progress = name();
            _progress += ": Sent";
            return Error::Eof;
        default:
            _progress = "";
            return err;
    }
}

MacroChannel::~MacroChannel() {}
