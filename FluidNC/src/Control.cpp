// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Control.h"

#include "Protocol.h"  // rtSafetyDoor, etc

Control::Control() :
    _safetyDoor(rtSafetyDoor, "Door", 'D'), _reset(rtReset, "Reset", 'R'), _feedHold(rtFeedHold, "FeedHold", 'H'),
    _cycleStart(rtCycleStart, "CycleStart", 'S'), _macro0(rtButtonMacro0, "Macro 0", '0'), _macro1(rtButtonMacro1, "Macro 1", '1'),
    _macro2(rtButtonMacro2, "Macro 2", '2'), _macro3(rtButtonMacro3, "Macro 3", '3'), _macro4(rtButtonMacro4, "Macro 4", '4'),
    _macro5(rtButtonMacro5, "Macro 5", '5'), _macro6(rtButtonMacro6, "Macro 6", '6'), _macro7(rtButtonMacro7, "Macro 7", '7'),
    _macro8(rtButtonMacro8, "Macro 8", '8'), _macro9(rtButtonMacro9, "Macro 9", '9') {}

void Control::init() {
    _safetyDoor.init();
    _reset.init();
    _feedHold.init();
    _cycleStart.init();
    _macro0.init();
    _macro1.init();
    _macro2.init();
    _macro3.init();
    _macro4.init();
    _macro5.init();
    _macro6.init();
    _macro7.init();
    _macro8.init();
    _macro9.init();
}

void Control::group(Configuration::HandlerBase& handler) {
    handler.item("safety_door_pin", _safetyDoor._pin);
    handler.item("reset_pin", _reset._pin);
    handler.item("feed_hold_pin", _feedHold._pin);
    handler.item("cycle_start_pin", _cycleStart._pin);
    handler.item("macro0_pin", _macro0._pin);
    handler.item("macro1_pin", _macro1._pin);
    handler.item("macro2_pin", _macro2._pin);
    handler.item("macro3_pin", _macro3._pin);
    handler.item("macro4_pin", _macro4._pin);
    handler.item("macro5_pin", _macro5._pin);
    handler.item("macro6_pin", _macro6._pin);
    handler.item("macro7_pin", _macro7._pin);
    handler.item("macro8_pin", _macro8._pin);
    handler.item("macro9_pin", _macro9._pin);
}

String Control::report() {
    return _safetyDoor.report() + _safetyDoor.report() + _reset.report() + _feedHold.report() + _cycleStart.report() + 
            _macro0.report() + _macro1.report() + _macro2.report() + _macro3.report() + _macro4.report() +
            _macro5.report() + _macro6.report() + _macro7.report() + _macro8.report() + _macro9.report();
}

bool Control::stuck() {
    return _safetyDoor.get() || _reset.get() || _feedHold.get() || _cycleStart.get() ||
           _macro0.get() || _macro1.get() || _macro2.get() ||_macro3.get() || _macro4.get() ||
           _macro5.get() || _macro6.get() || _macro7.get() || _macro8.get() || _macro9.get();
}

// Returns if safety door is ajar(T) or closed(F), based on pin state.
bool Control::system_check_safety_door_ajar() {
    // If a safety door pin is not defined, this will return false
    // because that is the default for the value field, which will
    // never be changed for an undefined pin.
    return _safetyDoor.get();
}
