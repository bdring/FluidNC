#pragma once

/*
    UserOutput.h

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

#include <cstdint>
#include "Pin.h"

namespace UserOutput {
    const uint8_t UNDEFINED_OUTPUT = 255;

    class DigitalOutput {
    public:
        DigitalOutput(size_t number, Pin& pin);

        bool        set_level(bool isOn);
        static void init_all();
        static void all_off();
        static bool set_level(size_t number, bool isOn);

    protected:
        void init();
        void config_message();

        uint8_t _number = UNDEFINED_OUTPUT;
        Pin&    _pin;
    };

    class AnalogOutput {
    public:
        AnalogOutput(uint8_t number, Pin& pin, uint32_t pwm_frequency);
        bool     set_level(uint32_t numerator);
        uint32_t denominator() { return 1UL << _resolution_bits; };

    protected:
        void init();
        void config_message();

        uint8_t  _number = UNDEFINED_OUTPUT;
        Pin&     _pin;
        uint8_t  _pwm_channel = -1;  // -1 means invalid or not setup
        uint32_t _pwm_frequency;
        uint8_t  _resolution_bits;
        uint32_t _current_value;
    };
}
