// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Control.h"

#include "Protocol.h"        // *Event
#include "Machine/Macros.h"  // macro0Event

Control::Control() {
    // The SafetyDoor pin must be defined first because it is checked explicity in safety_door_ajar()
    _pins.push_back(new ControlPin(&safetyDoorEvent, "safety_door_pin", 'D'));
    _pins.push_back(new ControlPin(&rtResetEvent, "reset_pin", 'R'));
    _pins.push_back(new ControlPin(&feedHoldEvent, "feed_hold_pin", 'H'));
    _pins.push_back(new ControlPin(&cycleStartEvent, "cycle_start_pin", 'S'));
    _pins.push_back(new ControlPin(&macro0Event, "macro0_pin", '0'));
    _pins.push_back(new ControlPin(&macro1Event, "macro1_pin", '1'));
    _pins.push_back(new ControlPin(&macro2Event, "macro2_pin", '2'));
    _pins.push_back(new ControlPin(&macro3Event, "macro3_pin", '3'));
    _pins.push_back(new ControlPin(&faultPinEvent, "fault_pin", 'F'));
    _pins.push_back(new ControlPin(&faultPinEvent, "estop_pin", 'E'));
}

void Control::init() {
    for (auto pin : _pins) {
        pin->init();
    }
}

void Control::group(Configuration::HandlerBase& handler) {
    for (auto pin : _pins) {
        handler.item(pin->_legend.c_str(), pin->_pin);
    }
}

std::string Control::report_status() {
    std::string ret = "";
    for (auto pin : _pins) {
        if (pin->get()) {
            ret += pin->letter();
        }
    }
    return ret;
}

bool Control::stuck() {
    for (auto pin : _pins) {
        if (pin->get()) {
            return true;
        }
    }
    return false;
}

bool Control::startup_check() {
    bool ret = false;
    for (auto pin : _pins) {
        if (pin->get()) {
            log_error(pin->_legend << " is active at startup");
            ret = true;
        }
    }
    return ret;
}

// Returns if safety door is ajar(T) or closed(F), based on pin state.
bool Control::safety_door_ajar() {
    // If a safety door pin is not defined, this will return false
    // because that is the default for the value field, which will
    // never be changed for an undefined pin.
    return _pins[0]->get();
}
