// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"

#include "GCode.h"  // CoolantState

class CoolantControl : public Configuration::Configurable {
    Pin _mist;
    Pin _flood;

    uint32_t _delay_ms = 0;

    CoolantState _previous_state = {};

    void write(CoolantState state);

public:
    CoolantControl() = default;

    bool hasMist() const { return _mist.defined(); }
    bool hasFlood() const { return _flood.defined(); }

    // Initializes coolant control pins.
    void init();

    // Returns current coolant output state. Overrides may alter it from programmed state.
    CoolantState get_state();

    // Immediately disables coolant pins.
    void stop();

    // Sets the coolant pins according to state specified.
    void off();
    void set_state(CoolantState state);

    // Configuration handlers.
    void group(Configuration::HandlerBase& handler) override;

    ~CoolantControl() = default;
};
