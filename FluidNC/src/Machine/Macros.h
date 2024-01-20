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
    class Macro {
    public:
        std::string _gcode;
        const char* _name;
        Macro(const char* name);
        bool run();
    };

    class Macros : public Configuration::Configurable {
    public:
        static const int n_startup_lines = 2;
        static const int n_macros        = 4;

        static Macro _macro[n_macros];
        static Macro _startup_line[n_startup_lines];
        static Macro _after_homing;
        static Macro _after_reset;
        static Macro _after_unlock;

        Macros() = default;

        // Configuration helpers:

        // TODO: We could validate the startup lines

        void group(Configuration::HandlerBase& handler) override {
            handler.item(_startup_line[0]._name, _startup_line[0]._gcode);
            handler.item(_startup_line[1]._name, _startup_line[1]._gcode);
            handler.item(_macro[0]._name, _macro[0]._gcode);
            handler.item(_macro[1]._name, _macro[1]._gcode);
            handler.item(_macro[2]._name, _macro[2]._gcode);
            handler.item(_macro[3]._name, _macro[3]._gcode);
            handler.item(_after_homing._name, _after_homing._gcode);
            handler.item(_after_reset._name, _after_reset._gcode);
            handler.item(_after_unlock._name, _after_unlock._gcode);
        }

        ~Macros() {}
    };
}
