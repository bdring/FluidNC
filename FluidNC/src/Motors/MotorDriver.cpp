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
#include "Machine/MachineConfig.h"
#include "Limit.h"  // limitsMinPosition
namespace MotorDrivers {
    std::string MotorDriver::axisName() const {
        return std::string(Axes::axisName(axis_index())) + (dual_axis_index() ? "2" : "") + " Axis";
    }

    void MotorDriver::debug_message() {}

    bool MotorDriver::test() {
        return true;
    };  // true = OK

    axis_t MotorDriver::axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called");
        return Axes::findAxisIndex(this);
    }
    motor_t MotorDriver::dual_axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called");
        return Axes::findAxisMotor(this);
    }
    void IRAM_ATTR MotorDriver::set_disable(bool disable) {}
}
