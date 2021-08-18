/*
  CoolantControl.cpp - coolant control methods
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring. This file was modifed for use on the ESP32
					    CPU. Do not use this with Grbl for atMega328P
    2021 - Stefan de Bruijn. Introduced classes and configuration for 
                             coolant control.

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

#include "CoolantControl.h"

#include "Protocol.h"  // protocol_buffer_synchronize

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
        _flood.write(pinState);
    }

    if (_mist.defined()) {
        bool pinState = state.Mist;
        _mist.write(pinState);
    }
}

// Directly called by coolant_init(), coolant_set_state(), and mc_reset(), which can be at
// an interrupt-level. No report flag set, but only called by routines that don't need it.
void CoolantControl::stop() {
    CoolantState disable = {};
    write(disable);
}

// Main program only. Immediately sets flood coolant running state and also mist coolant,
// if enabled. Also sets a flag to report an update to a coolant state.
// Called by coolant toggle override, parking restore, parking retract, sleep mode, g-code
// parser program end, and g-code parser CoolantControl::sync().

void CoolantControl::set_state(CoolantState state) {
    if (sys.abort) {
        return;  // Block during abort.
    }
    write(state);
    delay_msec(_delay_ms, DwellMode::SysSuspend);
}

void CoolantControl::off() {
    CoolantState disable = {};
    set_state(disable);
}

// G-code parser entry-point for setting coolant state. Forces a planner buffer sync and bails
// if an abort or check-mode is active.
void CoolantControl::sync(CoolantState state) {
    if (sys.state == State::CheckMode) {
        return;
    }
    protocol_buffer_synchronize();  // Ensure coolant turns on when specified in program.
    set_state(state);
}

void CoolantControl::group(Configuration::HandlerBase& handler) {
    handler.item("flood", _flood);
    handler.item("mist", _mist);
    handler.item("delay_ms", _delay_ms);
}
