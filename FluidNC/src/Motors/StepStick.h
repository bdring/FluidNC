#pragma once

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

#include "StandardStepper.h"

namespace MotorDrivers {
    class StepStick : public StandardStepper {
        Pin _MS1;
        Pin _MS2;
        Pin _MS3;
        Pin _Reset;

    public:
        StepStick() = default;

        void init() override;

        // Configuration handlers:
        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        void afterParse() override;

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override;

        ~StepStick() = default;
    };
}
