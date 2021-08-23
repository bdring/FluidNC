#pragma once

/*
	10vSpindle.h

	This is basically a PWM spindle with some changes, so a separate forward and
	reverse signal can be sent.

	The direction pins will act as enables for the 2 directions. There is usually
	a min RPM with VFDs, that speed will remain even if speed is 0. You
	must turn off both direction pins when enable is off.


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

namespace Spindles {
    class _10v : public PWM {
    public:
        _10v() = default;

        _10v(const _10v&) = delete;
        _10v(_10v&&)      = delete;
        _10v& operator=(const _10v&) = delete;
        _10v& operator=(_10v&&) = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;

        void deinit() override;

        // Configuration handlers:
        void validate() const override { PWM::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("forward", _forward_pin);
            handler.item("reverse", _reverse_pin);
            PWM::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "10V"; }

        ~_10v() {}

    protected:
        void set_enable(bool enable_pin);
        void set_direction(bool Clockwise);

        Pin _forward_pin;
        Pin _reverse_pin;
    };
}
