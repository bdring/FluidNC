// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    TODO
        Make sure public/private/protected is cleaned up.
        Only a few Unipolar axes have been setup in init()
        Get rid of Z_SERVO, just reply on Z_SERVO_PIN
        Class is ready to deal with non SPI pins, but they have not been needed yet.
            It would be nice in the config message though
    Testing
        Done (success)
            3 Axis (3 Standard Steppers)
            MPCNC (dual-motor axis with shared direction pin)
            TMC2130 Pen Laser (trinamics, stallguard tuning)
            Unipolar
        TODO
            4 Axis SPI (Daisy Chain, dual-motor axis with unique direction pins)
    Reference
        TMC2130 Datasheet https://www.trinamic.com/fileadmin/assets/Products/ICs_Documents/TMC2130_datasheet.pdf
*/

#include "MotorDriver.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // limitsMinPosition

namespace MotorDrivers {
    String MotorDriver::axisName() const {
        return String(config->_axes->axisName(axis_index())) + (dual_axis_index() ? "2" : "") + " Axis";
    }

    void MotorDriver::debug_message() {}

    bool MotorDriver::test() { return true; };  // true = OK

    size_t MotorDriver::axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called.");
        return size_t(config->_axes->findAxisIndex(this));
    }
    size_t MotorDriver::dual_axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called.");
        return size_t(config->_axes->findAxisMotor(this));
    }
    void IRAM_ATTR MotorDriver::set_disable(bool disable) {}
    void IRAM_ATTR MotorDriver::set_direction(bool) {}
    void IRAM_ATTR MotorDriver::step() {}
    void IRAM_ATTR MotorDriver::unstep() {}
}
