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
// #include "Axes.h"
#include "Motor.h"
#include "Homing.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Axis : public Configuration::Configurable {
        int _axis;

    public:
        Axis(int currentAxis) : _axis(currentAxis) {
            for (int i = 0; i < MAX_MOTORS_PER_AXIS; ++i) {
                _motors[i] = nullptr;
            }
        }

        static const int MAX_MOTORS_PER_AXIS = 2;

        Motor*  _motors[MAX_MOTORS_PER_AXIS];
        Homing* _homing = nullptr;

        float _stepsPerMm   = 320.0f;
        float _maxRate      = 1000.0f;
        float _acceleration = 25.0f;
        float _maxTravel    = 200.0f;
        bool  _softLimits   = false;

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;

        // Checks if a motor matches this axis:
        bool  hasMotor(const MotorDrivers::MotorDriver* const driver) const;
        bool  hasDualMotor();
        int   motorsWithSwitches();
        float pulloffOffset();

        void init();

        ~Axis();
    };
}
