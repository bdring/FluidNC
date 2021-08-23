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

#include <cstdint>

#include "PinCapabilities.h"

namespace Pins {
    /*
    Pin attributes are what a pin _should_ do in the current configuration. Note that there's 
    an overlap with pin capabilities; in fact, pin attributes are validated with pin capabilities
    to check if they are valid.

    You should use pin attributes in the same way as 'set mode' in native Arduino. When setting pin
    attributes, you basically specify some behavior that you want, and if the pin is capable of 
    delivering these attributes, it will work. Otherwise... you will get a very nasty error.
    */
    class PinAttributes {
        uint32_t _value;

        constexpr PinAttributes(const uint32_t value) : _value(value) {}

    public:
        PinAttributes(const PinAttributes&) = default;
        PinAttributes& operator=(const PinAttributes&) = default;

        // All the capabilities we use and test:
        static PinAttributes Undefined;
        static PinAttributes None;

        static PinAttributes Input;
        static PinAttributes Output;
        static PinAttributes PullUp;
        static PinAttributes PullDown;
        static PinAttributes ISR;

        static PinAttributes ActiveLow;
        static PinAttributes Exclusive;
        static PinAttributes InitialOn;

        inline PinAttributes operator|(PinAttributes rhs) { return PinAttributes(_value | rhs._value); }
        inline PinAttributes operator&(PinAttributes rhs) { return PinAttributes(_value & rhs._value); }
        inline bool          operator==(PinAttributes rhs) const { return _value == rhs._value; }
        inline bool          operator!=(PinAttributes rhs) const { return _value != rhs._value; }

        inline operator bool() { return _value != 0; }

        bool        conflictsWith(PinAttributes t);
        bool        validateWith(PinCapabilities caps);
        inline bool has(PinAttributes attr) { return ((*this) & attr).operator bool(); }
    };
}
