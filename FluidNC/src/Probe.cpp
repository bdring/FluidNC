// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Probe.h"
#include "Machine/EventPin.h"
#include "Machine/MachineConfig.h"

extern void    protocol_do_probe(void* arg);
const ArgEvent probeEvent { protocol_do_probe };

Probe::ProbeEventPin::ProbeEventPin(const char* legend) : EventPin(&probeEvent, ExecAlarm::None, legend) {}

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
    handler.item("probe_hard_limit", _probe_hard_limit);  // non probing motion protection
}
void protocol_do_probe(void* arg) {
    Probe* p = config->_probe;
    if (p->tripped() && probing) {
        probing = false;
        get_steps(probe_steps);
        if (p->_hard_stop) {
            Stepper::reset();
            plan_reset();
            state_is(State::Idle);
        } else {
            protocol_do_motion_cancel();
        }
    } else if (p->tripped() && p->_probe_hard_limit) {
        // trip if in homing, cycle or jog, but not probing
        if ((state_is(State::Cycle) || state_is(State::Jog) || state_is(State::Homing)) &&
            (gc_state.modal.motion != Motion::ProbeAway && gc_state.modal.motion != Motion::ProbeAwayNoError &&
             gc_state.modal.motion != Motion::ProbeToward && gc_state.modal.motion != Motion::ProbeTowardNoError)) {
            mc_critical(ExecAlarm::ProbeHardLimit);
        }
    }
}
