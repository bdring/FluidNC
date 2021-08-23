#pragma once

/*
	DacSpindle.h

	This uses the Analog DAC in the ESP32 to generate a voltage
	proportional to the GCode S value desired. Some spindle uses
	a 0-5V or 0-10V value to control the spindle. You would use
	an Op Amp type circuit to get from the 0.3.3V of the ESP32 to that voltage.

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

#include "OnOffSpindle.h"

#include <cstdint>

namespace Spindles {
    // This uses one of the (2) DAC pins on ESP32 to output a voltage
    class Dac : public OnOff {
    public:
        Dac() = default;

        Dac(const Dac&) = delete;
        Dac(Dac&&)      = delete;
        Dac& operator=(const Dac&) = delete;
        Dac& operator=(Dac&&) = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;

        // Configuration handlers:
        // Inherited from PWM

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "DAC"; }

        ~Dac() {}

    private:
        bool _gpio_ok;  // DAC is on a valid pin

    protected:
        void set_output(uint32_t duty);  // sets DAC instead of PWM
    };
}
