/*
  Probe.cpp - code pertaining to probing methods
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

#include "Probe.h"

#include "Pin.h"

// Probe pin initialization routine.
void Probe::init() {
    static bool show_init_msg = true;  // used to show message only once.

    if (_probePin.defined()) {
        _probePin.setAttr(Pin::Attr::Input);

        if (show_init_msg) {
            _probePin.report("Probe Pin:");
            show_init_msg = false;
        }
    }
}

void Probe::set_direction(bool is_away) {
    _isProbeAway = is_away;
}

// Returns the probe pin state. Triggered = true. Called by gcode parser.
bool Probe::get_state() {
    return _probePin.read();
}

// Returns true if the probe pin is tripped, accounting for the direction (away or not).
// This function must be extremely efficient as to not bog down the stepper ISR.
// Should be called only in situations where the probe pin is known to be defined.
bool IRAM_ATTR Probe::tripped() {
    return _probePin.read() ^ _isProbeAway;
}

void Probe::validate() const {}

void Probe::group(Configuration::HandlerBase& handler) {
    handler.item("pin", _probePin);
    handler.item("check_mode_start", _check_mode_start);
}
