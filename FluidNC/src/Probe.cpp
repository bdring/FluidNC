// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"
#include "Machine/EventPin.h"
#include "Machine/MachineConfig.h"

extern void    protocol_do_probe(void* arg);
const ArgEvent probeEvent { protocol_do_probe };

Probe::ProbeEventPin::ProbeEventPin(const char* legend) : EventPin(&probeEvent, legend) {}

void Probe::init() {
    _probePin.init();
    _toolsetterPin.init();
}

void Probe::set_direction(bool away) {
    _away = away;
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    return _probePin.get() || _toolsetterPin.get();
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
bool Probe::tripped() {
    return get_state() ^ _away;
}

void Probe::validate() {}

void Probe::group(Configuration::HandlerBase& handler) {
    handler.item("pin", _probePin);
    handler.item("toolsetter_pin", _toolsetterPin);
    handler.item("check_mode_start", _check_mode_start);
    handler.item("hard_stop", _hard_stop);
}
void protocol_do_probe(void* arg) {
    Probe* p = config->_probe;
    if (p->tripped() && probing) {
        probing = false;
        get_motor_steps(probe_steps);
        if (p->_hard_stop) {
            Stepper::reset();
            plan_reset();
            sys.state = State::Idle;
        } else {
            protocol_do_motion_cancel();
        }
    }
}
