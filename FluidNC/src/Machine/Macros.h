// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "Event.h"
// #include <algorithm>  // std::replace()

class MacroEvent : public Event {
    objnum_t _num;

public:
    MacroEvent(int num) : _num(num) {}
    void run(void*) const override;
};

extern const MacroEvent macro0Event;
extern const MacroEvent macro1Event;
extern const MacroEvent macro2Event;
extern const MacroEvent macro3Event;

class Macro;
namespace Machine {
    class Macros : public Configuration::Configurable {
    public:
        static const objnum_t n_macros = 4;

        static Macro _macro[];
        static Macro _startup;
        static Macro _startup_line0;
        static Macro _startup_line1;
        static Macro _after_homing;
        static Macro _after_reset;
        static Macro _after_unlock;

        Macros() = default;

        // Configuration helpers:

        void group(Configuration::HandlerBase& handler) override {
            handler.item(_startup_line0.name(), _startup_line0);
            handler.item(_startup_line1.name(), _startup_line1);
            handler.item(_macro[0].name(), _macro[0]);
            handler.item(_macro[1].name(), _macro[1]);
            handler.item(_macro[2].name(), _macro[2]);
            handler.item(_macro[3].name(), _macro[3]);
            handler.item(_after_homing.name(), _after_homing);
            handler.item(_after_reset.name(), _after_reset);
            handler.item(_after_unlock.name(), _after_unlock);
        }

        ~Macros() {}
    };

    class MacroChannel : public Channel {
    private:
        Error  _pending_error = Error::Ok;
        size_t _position      = 0;
        size_t _blank_lines   = 0;

        Macro* _macro;

        Error readLine(char* line, size_t maxlen);
        void  end_message();

    public:
        Error pollLine(char* line) override;

        MacroChannel(Macro* macro);

        MacroChannel(const MacroChannel&)            = delete;
        MacroChannel& operator=(const MacroChannel&) = delete;

        // Channel methods
        size_t write(uint8_t c) override { return 0; }
        void   ack(Error status) override;

        ~MacroChannel();
    };
}
