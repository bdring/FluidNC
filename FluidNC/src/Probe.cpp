// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"

#include "Pin.h"

// Probe pin initialization routine.
void Probe::init() {
    static bool show_init_msg = true;  // used to show message only once.

    if (_probePin.defined()) {
        _probePin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        _probePin.setAttr(Pin::Attr::Input);

        if (show_init_msg) {
            _probePin.report("Probe Pin:");
            show_init_msg = false;
        }
    }

    if (_toolsetter_Pin.defined()) {
        _toolsetter_Pin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        _toolsetter_Pin.setAttr(Pin::Attr::Input);

        if (show_init_msg) {
            _toolsetter_Pin.report("Toolsetter Pin:");
        }
    }
    show_init_msg = false;
}

void Probe::set_direction(bool is_away) {
    _isProbeAway = is_away;
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    return (_probePin.read() || _toolsetter_Pin.read());
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
// This function must be extremely efficient as to not bog down the stepper ISR.
// Should be called only in situations where the probe pin is known to be defined.
bool IRAM_ATTR Probe::tripped() {
    return (_probePin.read() || _toolsetter_Pin.read()) ^ _isProbeAway;
}

void Probe::validate() {}

void Probe::group(Configuration::HandlerBase& handler) {
    handler.item("pin", _probePin);
    handler.item("toolsetter_pin", _toolsetter_Pin);
    handler.item("check_mode_start", _check_mode_start);
}
