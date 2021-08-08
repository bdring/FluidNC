/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn, Mitch Bradley

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Motor.h"

#include "../Config.h"
#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"
#include "Axes.h"

namespace Machine {
    void Motor::group(Configuration::HandlerBase& handler) {
        _negLimitPin = new LimitPin(_negPin, _axis, _motorNum, -1, _hardLimits);
        _posLimitPin = new LimitPin(_posPin, _axis, _motorNum, 1, _hardLimits);
        _allLimitPin = new LimitPin(_allPin, _axis, _motorNum, 0, _hardLimits);
        handler.item("limit_neg", _negPin);
        handler.item("limit_pos", _posPin);
        handler.item("limit_all", _allPin);
        handler.item("hard_limits", _hardLimits);
        handler.item("pulloff", _pulloff);
        MotorDrivers::MotorFactory::factory(handler, _driver);
    }

    void Motor::afterParse() {
        if (_driver == nullptr) {
            _driver = new MotorDrivers::Nullmotor();
        }
    }

    void Motor::init() {
        if (strcmp(_driver->name(), "null_motor") != 0) {
            set_bitnum(Machine::Axes::motorMask, _axis + 16 * _motorNum);
        }
        _driver->init();

        _negLimitPin->init();
        _posLimitPin->init();
        _allLimitPin->init();
    }

    bool Motor::hasSwitches() { return (_negPin.defined() || _negPin.defined() || _negPin.defined()); }

    Motor::~Motor() { delete _driver; }
}
