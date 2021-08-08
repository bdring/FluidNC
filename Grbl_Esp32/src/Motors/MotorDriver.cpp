/*
    MotorDriver.cpp
    Part of Grbl_ESP32
    2020 -	Bart Dring
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
    TODO
        Make sure public/private/protected is cleaned up.
        Only a few Unipolar axes have been setup in init()
        Get rid of Z_SERVO, just reply on Z_SERVO_PIN
        Class is ready to deal with non SPI pins, but they have not been needed yet.
            It would be nice in the config message though
    Testing
        Done (success)
            3 Axis (3 Standard Steppers)
            MPCNC (ganged with shared direction pin)
            TMC2130 Pen Laser (trinamics, stallguard tuning)
            Unipolar
        TODO
            4 Axis SPI (Daisy Chain, Ganged with unique direction pins)
    Reference
        TMC2130 Datasheet https://www.trinamic.com/fileadmin/assets/Products/ICs_Documents/TMC2130_datasheet.pdf
*/

#include "MotorDriver.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // limitsMinPosition

namespace MotorDrivers {
    String MotorDriver::axisName() const { return String(config->_axes->axisName(axis_index())) + (dual_axis_index() ? "2" : "") + " Axis"; }
    String MotorDriver::axisLimits() const {
        return String("Limits(") + limitsMinPosition(axis_index()) + "," + limitsMaxPosition(axis_index()) + ")";
    }

    void MotorDriver::debug_message() {}

    bool MotorDriver::test() { return true; };  // true = OK

    uint8_t MotorDriver::axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called.");
        return uint8_t(config->_axes->findAxisIndex(this));
    }
    uint8_t MotorDriver::dual_axis_index() const {
        Assert(config != nullptr && config->_axes != nullptr, "Expected machine to be configured before this is called.");
        return uint8_t(config->_axes->findAxisGanged(this));
    }
}
