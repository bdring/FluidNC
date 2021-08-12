#pragma once

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

#include "../Configuration/Configurable.h"
#include "LimitPin.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Endstops;
}

namespace Machine {
    class Motor : public Configuration::Configurable {
        LimitPin* _negLimitPin;
        LimitPin* _posLimitPin;
        LimitPin* _allLimitPin;

        int _axis;
        int _motorNum;

    public:
        Motor(int axis, int motorNum) : _axis(axis), _motorNum(motorNum) {}

        MotorDrivers::MotorDriver* _driver  = nullptr;
        float                      _pulloff = 1.0f;  // mm

        Pin  _negPin;
        Pin  _posPin;
        Pin  _allPin;
        bool _hardLimits = true;

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        bool hasSwitches();
        bool isRealMotor();  // not a nullmotor
        void makeDualSwitches();
        void init();
        ~Motor();
    };
}
