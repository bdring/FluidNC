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
    // @config pin
    // @default NO_PIN
    // The probe input signal. G38 probing moves trigger on either edge of this pin.
    handler.item("pin", _probePin);

    // @config toolsetter_pin
    // @default NO_PIN
    // An optional second probe input, treated identically to pin -- G38 can't target one
    // or the other specifically, either triggers the same probe action. Having two lets
    // N.C./N.O. (or PNP/NPN) probes be wired without an external OR'ing circuit, and each
    // shows up as a separate switch in the '?' status report to help debug wiring.
    handler.item("toolsetter_pin", _toolsetterPin);

    // @config check_mode_start
    // @default true
    // Only affects Grbl Check Mode ($C, dry-run parsing with no real motion): while in
    // Check Mode, a probe move reports its position as the move's un-probed target when
    // false, or leaves it at the pre-move start position when true. Has no effect on a real
    // (non-Check-Mode) probe cycle.
    handler.item("check_mode_start", _check_mode_start);

    // @config hard_stop
    // @default false
    // On a successful probe trigger, stops with an immediate hard stop instead of
    // decelerating -- avoids the extra travel deceleration needs, at the cost of possibly
    // losing steps (and therefore position accuracy) at higher speeds. Useful with fragile
    // probes/bits that could be damaged by the overtravel a normal deceleration needs.
    handler.item("hard_stop", _hard_stop);

    // @config probe_hard_limit
    // @default false
    // When true, the probe pin(s) also act like a hard limit switch during non-probing
    // motion (homing, jog, cycle): triggering it raises an alarm and immediately stops
    // motion, the same as a real hard limit switch would. Guards against accidental probe
    // damage from a collision during ordinary motion, not from the probing move itself.
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
            set_state(State::Idle);
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
