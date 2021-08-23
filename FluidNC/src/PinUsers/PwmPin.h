/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

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

#pragma once

#include "../Pin.h"
#include "../Assert.h"

namespace PinUsers {
    class PwmDetail {
    public:
        virtual uint32_t getFrequency() const = 0;
        virtual uint32_t getMaxDuty() const   = 0;

        // Sets the PWM value from 0..1.
        virtual void setValue(float value) = 0;

        virtual ~PwmDetail() {}
    };

    class PwmPin {
        Pin        _pin;
        PwmDetail* _detail;

    public:
        PwmPin() : _detail(nullptr) {}
        PwmPin(Pin&& pin, uint32_t frequency, uint32_t maxDuty);

        // Returns actual frequency which might not be exactly the same as requested(nearest supported value)
        inline uint32_t getFrequency() const { return _detail->getFrequency(); }

        // Returns actual maxDuty which might not be exactly the same as requested(nearest supported value)
        inline uint32_t getMaxDuty() const { return _detail->getMaxDuty(); }

        inline void setValue(float value) const { return _detail->setValue(value); }

        inline ~PwmPin() { delete _detail; }
    };
}
