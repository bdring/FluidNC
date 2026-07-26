// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    Stepstick.cpp -- stepstick type stepper drivers
*/

#include "StepStick.h"

namespace MotorDrivers {
    void StepStick::init() {
        // If they are not 'undefined', set them as 'on'.
        _MS1.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
        _MS2.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
        _MS3.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);

        StandardStepper::init();
    }

    // Configuration handlers:
    void StepStick::validate() {
        StandardStepper::validate();
    }

    void StepStick::group(Configuration::HandlerBase& handler) {
        StandardStepper::group(handler);

        // @config ms1_pin
        // @default NO_PIN
        // Microstep-select pin 1, for driver sockets (DRV8825/A4988/TB67S249FTG family)
        // that select microstepping via MSx pins rather than a register interface.
        handler.item("ms1_pin", _MS1);

        // @config ms2_pin
        // @default NO_PIN
        // Microstep-select pin 2.
        handler.item("ms2_pin", _MS2);

        // @config ms3_pin
        // @default NO_PIN
        // Microstep-select pin 3.
        handler.item("ms3_pin", _MS3);

        // @config reset_pin
        // @default NO_PIN
        // Driver reset pin. Only sets the pin's state once at startup (turned on if
        // defined) -- not an actively toggled runtime signal.
        handler.item("reset_pin", _Reset);
    }

    void StepStick::afterParse() {
        if (!_Reset.undefined()) {
            log_info("Using StepStick Mode");

            // !RESET pin on steppers  (MISO On Schematic)
            _Reset.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
            _Reset.on();
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<StepStick> registration("stepstick");
    }
}
