#pragma once

/*
  Probe.h - code pertaining to probing methods
  Part of Grbl

  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

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

public:
    // Configurable
    bool _check_mode_start = true;
    // _check_mode_start configures the position after a probing cycle
    // during check mode. false sets the position to the probe target,
    // true sets the position to the start position.

    Probe() = default;

    bool exists() const { return _probePin.defined(); }

    // Probe pin initialization routine.
    void init();

    // setup probing direction G38.2 vs. G38.4
    void set_direction(bool is_away);

    // Returns probe pin state. Triggered = true. Called by gcode parser and probe state monitor.
    bool get_state();

    // Returns true if the probe pin is tripped, depending on the direction (away or not)
    bool IRAM_ATTR tripped();

    // Configuration handlers.
    void validate() const override;
    void group(Configuration::HandlerBase& handler) override;

    ~Probe() = default;
};
