// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"
#include "Pin.h"

// Probe pin initialization routine.
void Probe::init() {
    if (_probePin.defined()) {
        try {
            _probePin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
            _probe_is_native = true;
        } catch (...) { _probe_is_native = false; }

        _probeEventPin = new ProbeEventPin("Probe", _probePin);
        _probeEventPin->init();
    }

    if (_toolsetterPin.defined()) {
        _toolsetterPin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
        _toolsetter_is_native = true;
    }
    try {
            _toolsetterPin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);
            _toolsetter_is_native = true;
        } catch (...) {
        _toolsetter_is_native = false;
    }
    _toolsetterPin.getNative(Pin::Capabilities::Input | Pin::Capabilities::Native);

    _toolsetterEventPin = new ProbeEventPin("Toolsetter", _toolsetterPin);
    _toolsetterEventPin->init();

}

void Probe::set_direction(bool is_away) {
    _isProbeAway = is_away;
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    // This is time-critical when called from the stepper IRQ so
    // we go to a lot of trouble to optimize the case where we
    // read the GPIOs directly
    // But we might be screwed by the vtable of EventPin
    if (_probe_is_native) {
        if (_probePin.read()) {
            return true;
        }
        if (_toolsetter_is_native) {
            return _toolsetterPin.read();
        }
        // Native probe pin was inactive
        return _toolsetterEventPin && _toolsetterEventPin->get();
    }
    if (_toolsetter_is_native) {
        if (_toolsetterPin.read()) {
            return true;
        }
        // Native toolsetter pin was inactive
        return _probeEventPin && _probeEventPin->get();
    }
    // Neither probe nor toolsetter was native so we must use the Event pins
    return ((_probeEventPin && _probeEventPin->get()) || (_toolsetterEventPin && _toolsetterEventPin->get()));
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
// This function must be extremely efficient as to not bog down the stepper ISR.
// Should be called only in situations where the probe pin is known to be defined.
bool IRAM_ATTR Probe::tripped() {
    return get_state() ^ _isProbeAway;
}

void Probe::validate() {}

void Probe::group(Configuration::HandlerBase& handler) {
    handler.item("pin", _probePin);
    handler.item("toolsetter_pin", _toolsetterPin);
    handler.item("check_mode_start", _check_mode_start);
}
