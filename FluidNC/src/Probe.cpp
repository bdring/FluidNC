// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"

#include "Pin.h"

// Probe pin initialization routine.
void Probe::init() {
    static bool show_init_msg = true;  // used to show message only once.

    for (auto it : _probes) {
        Probes::TripProbe ptr = &Probe::tripProbe;
        it->init(ptr, this);
    }
}

void IRAM_ATTR Probe::tripProbe(Probe* userData, int32_t tickDelta) {
    auto p           = static_cast<Probe*>(userData);
    p->_probeTripped = true;
}

void Probe::set_direction(bool is_away) {
    _isProbeAway = is_away;

    for (auto it : _probes) {
        // We have to think this over properly!!! Check all? Check some? What if some
        // thing tripped that was not in the vector?
        if (it->start_cycle(is_away)) {
            // put in vector?
        }
    }
}

void Probe::stop_probe() {
    for (auto it : _probes) {
        // We have to think this over properly!!!
        it->stop_cycle();
    }
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    return _probeTripped;
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
// This function must be extremely efficient as to not bog down the stepper ISR.
// Should be called only in situations where the probe pin is known to be defined.
bool IRAM_ATTR Probe::tripped() {
    // TODO: Trip index instead of just 'tripped'? Makes sense, see 'set_direction' above.
    // Or do we just want to trigger 'tripped' if anything changes? That's actually quite 
    // simple...

    return _probeTripped ^ _isProbeAway;
}

void Probe::validate() const {
    for (auto it : _probes) {
        it->validate();
    }
}

void Probe::group(Configuration::HandlerBase& handler) {
    Probes::ProbeFactory::factory(handler, _probes);

    handler.item("check_mode_start", _check_mode_start);  // TODO FIXME
}
