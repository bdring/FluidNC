// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Macros.h"
#include "../Serial.h"                 // Cmd
#include "../System.h"                 // sys
#include "../Machine/MachineConfig.h"  // config
#include "../Logging.h"

void MacroEvent::run(void* arg) {
    if (sys.state() != State::Idle) {
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

bool Macros::run_macro(size_t index) {
    if (index >= n_macros) {
        return false;
    }
    auto macro = _macro[index];
    if (macro == "") {
        return true;
    }

    char c;
    for (int i = 0; i < macro.length(); i++) {
        c = macro[i];
        switch (c) {
            case '&':
                // & is a proxy for newlines in macros, because you cannot
                // enter a newline directly in a config file string value.
                WebUI::inputBuffer.push('\n');
                break;
            case '#':
                if ((i + 3) > macro.length()) {
                    log_error("Missing characters after # realtime escape in macro");
                    return false;
                }
                {
                    std::string s(macro.c_str() + i + 1, 2);
                    Cmd         cmd = findOverride(s);
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
