/*
    Stepstick.cpp -- stepstick type stepper drivers

    2021 - Stefan de Bruijn

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
    void StepStick::validate() const { StandardStepper::validate(); }

    void StepStick::group(Configuration::HandlerBase& handler) {
        StandardStepper::group(handler);

        handler.item("ms1", _MS1);
        handler.item("ms2", _MS2);
        handler.item("ms3", _MS3);
        handler.item("reset", _Reset);
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
