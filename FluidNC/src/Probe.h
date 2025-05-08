// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"

#include <cstdint>

class Probe : public Configuration::Configurable {
    // Inverts the probe pin state depending on user settings and probing cycle mode.
    bool _away = false;

    class ProbeEventPin : public EventPin {
    public:
        ProbeEventPin(const char* legend);

        // Differs from the EventPin version by sending the event on either edge
        void trigger(bool active) override {
            InputPin::trigger(active);
            protocol_send_event(_event, this);
        }
    };

    ProbeEventPin _probePin;
    ProbeEventPin _toolsetterPin;

public:
    bool _hard_stop = false;
    // Configurable
    bool _check_mode_start = true;
    // _check_mode_start configures the position after a probing cycle
    // during check mode. false sets the position to the probe target,
    // true sets the position to the start position.

    Probe() : _probePin("Probe"), _toolsetterPin("Toolsetter") {}

    // Configurable
    bool exists() { return _probePin.defined() || _toolsetterPin.defined(); }

    void init();

    // setup probing direction G38.2 vs. G38.4
    void set_direction(bool away);

    // Returns probe pin state. Triggered = true. Called by gcode parser and probe state monitor.
    bool get_state();

    // Returns true if the probe pin is tripped, depending on the direction (away or not)
    bool IRAM_ATTR tripped();

    ProbeEventPin& probePin() { return _probePin; }
    ProbeEventPin& toolsetterPin() { return _toolsetterPin; }

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;

    ~Probe() = default;
};
