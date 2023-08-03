// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "src/Serial.h"                 // Cmd
#include "src/System.h"                 // sys
#include "src/Machine/MachineConfig.h"  // config

void MacroEvent::run(void* arg) {
    int n = int(arg);
    if (sys.state != State::Idle) {
        log_error("Macro can only be used in idle state");
        return;
    }
    log_debug("Macro " << n);
    config->_macros->run_macro(n);
}

std::string Macros::_startup_line[n_startup_lines];
std::string Macros::_macro[n_macros];
std::string Macros::_after_homing_line;
std::string Macros::_after_reset_line;
std::string Macros::_after_unlock_line;

MacroEvent macro0Event { 0 };
MacroEvent macro1Event { 1 };
MacroEvent macro2Event { 2 };
MacroEvent macro3Event { 3 };

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

bool Macros::run_macro(size_t n) {
    return run_macro(_macro[n]);
}

bool Macros::run_macro(const std::string& s) {
    if (s == "") {
        return true;
    }

    log_info("Running macro: " << s);
    char c;
    for (int i = 0; i < s.length(); i++) {
        c = s[i];
        switch (c) {
            case '&':
                // & is a proxy for newlines in macros, because you cannot
                // enter a newline directly in a config file string value.
                WebUI::inputBuffer.push('\n');
                break;
            case '#':
                if ((i + 3) > s.length()) {
                    log_error("Missing characters after # realtime escape in macro");
                    return false;
                }
                {
                    std::string s1(s.c_str() + i + 1, 2);
                    Cmd         cmd = findOverride(s1);
                    if (cmd == Cmd::None) {
                        log_error("Bad #xx realtime escape in macro");
                        return false;
                    }
                    WebUI::inputBuffer.push(static_cast<uint8_t>(cmd));
                }
                i += 3;
                break;
            default:
                WebUI::inputBuffer.push(c);
                break;
        }
    }
    WebUI::inputBuffer.push('\n');
    return true;
}
