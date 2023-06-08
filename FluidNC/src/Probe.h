// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/HandlerBase.h"
#include "Configuration/Configurable.h"

#include <cstdint>

// Values that define the probing state machine.
enum class ProbeState : uint8_t {
    Off    = 0,  // Probing disabled or not in use. (Must be zero.)
    Active = 1,  // Actively watching the input pin.
};

class Probe : public Configuration::Configurable {
    // Inverts the probe pin state depending on user settings and probing cycle mode.
    bool _isProbeAway = false;

    // Configurable
    Pin _probePin;
    Pin _toolsetter_Pin;

public:
    // Configurable
    bool _check_mode_start = true;
    // _check_mode_start configures the position after a probing cycle
    // during check mode. false sets the position to the probe target,
    // true sets the position to the start position.

    Probe() = default;

    bool exists() const { return _probePin.defined() || _toolsetter_Pin.defined(); }
    // Probe pin initialization routine.
    void init();

    // setup probing direction G38.2 vs. G38.4
    void set_direction(bool is_away);

    // Returns probe pin state. Triggered = true. Called by gcode parser and probe state monitor.
    bool get_state();

    // Returns true if the probe pin is tripped, depending on the direction (away or not)
    bool IRAM_ATTR tripped();

    // Configuration handlers.
    void validate() override;
    void group(Configuration::HandlerBase& handler) override;

    ~Probe() = default;
};
