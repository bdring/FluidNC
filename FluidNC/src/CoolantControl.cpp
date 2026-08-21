// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "CoolantControl.h"
#include "System.h"

#include <cstdint>

// Implemented by the optional WiFi output-url module.  Bluetooth/no-radio
// builds retain a null weak seam and do not enqueue network work.
extern "C" void fluidnc_output_url_transition(uint8_t logical_output, bool state) __attribute__((weak));

namespace {
    constexpr uint8_t OutputUrlFlood = 1;
    constexpr uint8_t OutputUrlMist  = 2;

    void notify_output_url_transition(uint8_t logical_output, bool state) {
        if (fluidnc_output_url_transition) {
            fluidnc_output_url_transition(logical_output, state);
        }
    }
}  // namespace

void CoolantControl::init() {
    static bool init_message = true;  // used to show messages only once.

    if (init_message) {
        _flood.report("Flood coolant");
        _mist.report("Mist coolant");
        init_message = false;
    }

    _flood.setAttr(Pin::Attr::Output);
    _mist.setAttr(Pin::Attr::Output);

    stop();
}

// Returns current coolant output state. Overrides may alter it from programmed state.
CoolantState CoolantControl::get_state() {
    CoolantState cl_state = {};

    if (_flood.defined()) {
        auto pinState = _flood.read();

        if (pinState) {
            cl_state.Flood = 1;
        }
    }

    if (_mist.defined()) {
        auto pinState = _mist.read();

        if (pinState) {
            cl_state.Mist = 1;
        }
    }

    return cl_state;
}

void CoolantControl::write(CoolantState state) {
    if (_flood.defined()) {
        bool pinState = state.Flood;
        _flood.synchronousWrite(pinState);
    }

    if (_mist.defined()) {
        bool pinState = state.Mist;
        _mist.synchronousWrite(pinState);
    }

    _previous_state = state;
}

// Directly called by coolant_init(), coolant_set_state(), which can be at
// an interrupt-level. No report flag set, but only called by routines that don't need it.
void CoolantControl::stop() {
    CoolantState disable = {};
    write(disable);
}

void CoolantControl::stop_and_notify() {
    const auto previous = _previous_state;
    stop();
    if (previous.Flood) {
        notify_output_url_transition(OutputUrlFlood, false);
    }
    if (previous.Mist) {
        notify_output_url_transition(OutputUrlMist, false);
    }
}

// Main program only. Immediately sets flood coolant running state and also mist coolant,
// if enabled. Also sets a flag to report an update to a coolant state.
// Called by coolant toggle override, parking restore, parking retract, sleep mode, g-code
// parser program end, and g-code parser CoolantControl::sync().

void CoolantControl::set_state(CoolantState state) {
    if (sys.abort() || (_previous_state.Mist == state.Mist && _previous_state.Flood == state.Flood)) {
        return;  // Block during abort or if no change
    }
    write(state);

    if (state.Mist || state.Flood)  // ignore delay on turn off
        dwell_ms(_delay_ms, DwellMode::SysSuspend);
}

void CoolantControl::off() {
    CoolantState disable = {};
    set_state(disable);
}

void CoolantControl::group(Configuration::HandlerBase& handler) {
    // @config flood_pin
    // @default NO_PIN
    // Controls a flood coolant device (traditionally a liquid coolant, though many machines
    // repurpose this output for other things, e.g. dust extraction). M8 turns it on, M9
    // turns it off.
    handler.item("flood_pin", _flood);

    // @config mist_pin
    // @default NO_PIN
    // Controls a mist coolant device. M7 turns it on, M9 turns it off.
    handler.item("mist_pin", _mist);

    // @config delay_ms
    // @default 0
    // Delay, in milliseconds, after M7/M8 turns a coolant output on, before motion resumes
    // -- gives the coolant device time to actually start flowing. Not applied if that
    // coolant output is already on, and not applied to M9 (turning off).
    handler.item("delay_ms", _delay_ms, 0, 10000);
}
