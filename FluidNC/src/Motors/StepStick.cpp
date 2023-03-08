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
    void StepStick::validate() { StandardStepper::validate(); }

    void StepStick::group(Configuration::HandlerBase& handler) {
        StandardStepper::group(handler);

        handler.item("ms1_pin", _MS1);
        handler.item("ms2_pin", _MS2);
        handler.item("ms3_pin", _MS3);
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

    // Name of the configurable. Must match the name registered in the cpp file.
    const char* StepStick::name() const { return "stepstick"; }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<StepStick> registration("stepstick");
    }
}
