// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Configuration/Configurable.h"
#include "src/WebUI/InputBuffer.h"  // WebUI::inputBuffer
#include "src/UartChannel.h"
#include "src/Event.h"
#include <algorithm>

class MacroEvent : public Event {
    int _num;

public:
    MacroEvent(int num) : _num(num) {}
    void run(void*) override;
};

extern MacroEvent macro0Event;
extern MacroEvent macro1Event;
extern MacroEvent macro2Event;
extern MacroEvent macro3Event;

namespace Machine {
    class Macros : public Configuration::Configurable {
    public:
        static const int n_startup_lines = 2;
        static const int n_macros        = 4;

    private:
        static std::string _macro[n_macros];

    public:
        static std::string _startup_line[n_startup_lines];
        static std::string _after_homing_line;
        static std::string _after_reset_line;
        static std::string _after_unlock_line;

        Macros() = default;

        bool run_macro(size_t index);
        bool run_macro(const std::string& s);

        // Configuration helpers:

        // TODO: We could validate the startup lines

        void group(Configuration::HandlerBase& handler) override {
            handler.item("startup_line0", _startup_line[0]);
            handler.item("startup_line1", _startup_line[1]);
            handler.item("macro0", _macro[0]);
            handler.item("macro1", _macro[1]);
            handler.item("macro2", _macro[2]);
            handler.item("macro3", _macro[3]);
            handler.item("after_homing", _after_homing_line);
            handler.item("after_reset", _after_reset_line);
            handler.item("after_unlock", _after_unlock_line);
        }

        ~Macros() {}
    };
}
