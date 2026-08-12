// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Control.h"

#include "Protocol.h"        // *Event
#include "Machine/Macros.h"  // macro0Event

Control::Control() {
    // All control inputs (Pin, input, default NO_PIN) must read non-active at boot -- this
    // guards against starting with a stuck switch, and raises an "active at startup" alarm
    // if one already reads active on reset. Active state is inverted with the pin's :low
    // attribute. Status is reported via the '?' status command.
    //
    // The first 4 pins below (safety_door/reset/feed_hold/cycle_start) match standard Grbl
    // v1.1 control pins and trigger the same actions as their real-time-character
    // equivalents; a Grbl-only sender won't recognize the rest (macroN/fault/estop/homing).

    // The SafetyDoor pin must be defined first because it is checked explicitly in safety_door_ajar()

    // @config safety_door_pin
    // @default NO_PIN
    // @pin_attributes input
    // Typically wired to an enclosure door switch. While active, quickly stops motion and
    // enters Door mode (often paired with the parking feature); the switch must be
    // deactivated before the machine can be used. If it paused a running job, closing the
    // door and issuing cycle_start (a sender's resume button, or a cycle_start_pin switch)
    // resumes the job.
    _pins.push_back(new ControlPin(&safetyDoorEvent, "safety_door_pin", 'D'));

    // @config reset_pin
    // @default NO_PIN
    // @pin_attributes input
    // Performs a soft reset, equivalent to sending the Ctrl-X real-time character.
    _pins.push_back(new ControlPin(&rtResetEvent, "reset_pin", 'R'));

    // @config feed_hold_pin
    // @default NO_PIN
    // @pin_attributes input
    // Pauses a running job, equivalent to sending the '!' real-time character. Paired with
    // cycle_start_pin, lets a job be paused/resumed with physical buttons.
    _pins.push_back(new ControlPin(&feedHoldEvent, "feed_hold_pin", 'H'));

    // @config cycle_start_pin
    // @default NO_PIN
    // @pin_attributes input
    // Resumes a paused job, equivalent to sending the '~' real-time character. Paired with
    // feed_hold_pin, lets a job be paused/resumed with physical buttons.
    _pins.push_back(new ControlPin(&cycleStartEvent, "cycle_start_pin", 'S'));

    // @config macro0_pin
    // @default NO_PIN
    // @pin_attributes input
    // Runs macro0 (configured under macros:) when active, equivalent to sending the 0x87
    // real-time character.
    _pins.push_back(new ControlPin(&macro0Event, "macro0_pin", '0'));

    // @config macro1_pin
    // @default NO_PIN
    // @pin_attributes input
    // Runs macro1 (configured under macros:) when active, equivalent to sending the 0x88
    // real-time character.
    _pins.push_back(new ControlPin(&macro1Event, "macro1_pin", '1'));

    // @config macro2_pin
    // @default NO_PIN
    // @pin_attributes input
    // Runs macro2 (configured under macros:) when active, equivalent to sending the 0x89
    // real-time character.
    _pins.push_back(new ControlPin(&macro2Event, "macro2_pin", '2'));

    // @config macro3_pin
    // @default NO_PIN
    // @pin_attributes input
    // Runs macro3 (configured under macros:) when active, equivalent to sending the 0x8a
    // real-time character.
    _pins.push_back(new ControlPin(&macro3Event, "macro3_pin", '3'));

    // @config fault_pin
    // @default NO_PIN
    // @pin_attributes input
    // Immediate hard stop (no deceleration, so position accuracy can be lost) and a
    // critical alarm that only a soft reset can clear; also stops the spindle if the active
    // spindle's off_on_alarm is true. Critical alarm state blocks homing and unlock.
    // Functionally identical to estop_pin -- this one is intended for a machine-detected
    // fault signal (e.g. a stepper driver's fault/alarm output), not a user-operated switch.
    _pins.push_back(new ControlPin(&faultPinEvent, "fault_pin", 'F'));

    // @config estop_pin
    // @default NO_PIN
    // @pin_attributes input
    // Functionally identical to fault_pin (same hard stop + critical alarm behavior) --
    // this one is intended for a user-operated emergency-stop switch. Note that this alone
    // is only a control-input-level stop; a true e-stop should also cut power directly.
    _pins.push_back(new ControlPin(&faultPinEvent, "estop_pin", 'E'));

    // @config homing_button_pin
    // @default NO_PIN
    // @pin_attributes input
    // Runs $H (home all) when active.
    _pins.push_back(new ControlPin(&homingButtonEvent, "homing_button_pin", 'O'));
}

void Control::init() {
    for (auto pin : _pins) {
        pin->init();
    }
}

void Control::group(Configuration::HandlerBase& handler) {
    for (auto pin : _pins) {
        handler.item(pin->legend(), *pin);
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

bool Control::pins_block_unlock() {
    std::string blockers("FE");  // Fault, E-Stop block unlock and homing
    for (auto pin : _pins) {
        if (pin->get() && blockers.find(pin->letter()) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool Control::stuck() {
    for (auto pin : _pins) {
        if (pin->get()) {
            return true;
        }
    }
    return false;
}

// Returns if safety door is ajar(T) or closed(F), based on pin state.
bool Control::safety_door_ajar() {
    // If a safety door pin is not defined, this will return false
    // because that is the default for the value field, which will
    // never be changed for an undefined pin.
    return _pins[0]->get();
}
