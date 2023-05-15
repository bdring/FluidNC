// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Configuration/Configurable.h"
#include "src/Macro.h"
#include "src/Event.h"
// #include <algorithm>  // std::replace()

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

class Macro;

namespace Machine {
    class Macros : public Configuration::Configurable {
    public:
        static const int n_macros = 4;

    protected:
        static Macro _startup;
        static Macro _macro[];

    public:
        Macros() = default;

        bool run_startup();
        bool run_macro(size_t index);

        // Configuration helpers:

        // TODO: We could validate the startup lines

        void group(Configuration::HandlerBase& handler) override {
            handler.item("startup", _startup);
            handler.item("macro0", _macro[0]);
            handler.item("macro1", _macro[1]);
            handler.item("macro2", _macro[2]);
            handler.item("macro3", _macro[3]);
        }

        ~Macros() {}
    };

    class MacroChannel : public Channel {
    private:
        Error  _pending_error = Error::Ok;
        size_t _position      = 0;

        Macro* _macro;

        Error readLine(char* line, int maxlen);

    public:
        Error pollLine(char* line) override;

        MacroChannel(Macro* macro);

        MacroChannel(const MacroChannel&) = delete;
        MacroChannel& operator=(const MacroChannel&) = delete;

        // Channel methods
        size_t write(uint8_t c) override { return 0; }
        void   ack(Error status) override;

        ~MacroChannel();
    };
}
