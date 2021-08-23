#pragma once

/*
	Laser.h

	This is similar to the PWM Spindle except that it allows the
	M4 speed vs. power copensation.

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

*/
#include "PWMSpindle.h"

#include <cstdint>

namespace Spindles {
    // this is the same as a PWM spindle but the M4 compensation is supported.
    class Laser : public PWM {
    public:
        Laser() = default;

        Laser(const Laser&) = delete;
        Laser(Laser&&)      = delete;
        Laser& operator=(const Laser&) = delete;
        Laser& operator=(Laser&&) = delete;

        bool isRateAdjusted() override;
        void config_message() override;
        void get_pins_and_settings() override;
        void set_direction(bool Clockwise) override {};
        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Laser"; }

        void group(Configuration::HandlerBase& handler) override { PWM::group(handler); }

        ~Laser() {}
    };
}
