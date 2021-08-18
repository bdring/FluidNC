/*
  Control.cpp - input pins
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC

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

#include "Control.h"

#include "Protocol.h"  // rtSafetyDoor, etc

Control::Control() :
    _safetyDoor(rtSafetyDoor, "Door", 'D'), _reset(rtReset, "Reset", 'R'), _feedHold(rtFeedHold, "FeedHold", 'H'),
    _cycleStart(rtCycleStart, "CycleStart", 'S'), _macro0(rtButtonMacro0, "Macro 0", '0'), _macro1(rtButtonMacro1, "Macro 1", '1'),
    _macro2(rtButtonMacro2, "Macro 2", '2'), _macro3(rtButtonMacro3, "Macro 3", '3') {}

void Control::init() {
    _safetyDoor.init();
    _reset.init();
    _feedHold.init();
    _cycleStart.init();
    _macro0.init();
    _macro1.init();
    _macro2.init();
    _macro3.init();
}

void Control::group(Configuration::HandlerBase& handler) {
    handler.item("safety_door", _safetyDoor._pin);
    handler.item("reset", _reset._pin);
    handler.item("feed_hold", _feedHold._pin);
    handler.item("cycle_start", _cycleStart._pin);
    handler.item("macro0", _macro0._pin);
    handler.item("macro1", _macro1._pin);
    handler.item("macro2", _macro2._pin);
    handler.item("macro3", _macro3._pin);
}

void Control::report(char* status) {
    _safetyDoor.report(status);
    _reset.report(status);
    _feedHold.report(status);
    _cycleStart.report(status);
    _macro0.report(status);
    _macro1.report(status);
    _macro2.report(status);
    _macro3.report(status);
}

// Returns if safety door is ajar(T) or closed(F), based on pin state.
bool Control::system_check_safety_door_ajar() {
    // If a safety door pin is not defined, this will return false
    // because that is the default for the value field, which will
    // never be changed for an undefined pin.
    return _safetyDoor.get();
}
