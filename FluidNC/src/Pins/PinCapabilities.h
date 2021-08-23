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

namespace Pins {
    class PinAttributes;

    /*
    Pin capabilities are what a pin _can_ do using the internal hardware. For GPIO pins, these
    are the internal hardware capabilities of the pins, such as the capability to pull-up from
    hardware, wether or not a pin supports input/output, etc.
    */
    class PinCapabilities {
        uint32_t _value;

        constexpr PinCapabilities(const uint32_t value) : _value(value) {}

        friend class PinAttributes;  // Wants access to _value for validation

    public:
        PinCapabilities(const PinCapabilities&) = default;
        PinCapabilities& operator=(const PinCapabilities&) = default;

        // All the capabilities we use and test:
        static PinCapabilities None;

        static PinCapabilities Input;     // NOTE: Mapped in PinAttributes!
        static PinCapabilities Output;    // NOTE: Mapped in PinAttributes!
        static PinCapabilities PullUp;    // NOTE: Mapped in PinAttributes!
        static PinCapabilities PullDown;  // NOTE: Mapped in PinAttributes!
        static PinCapabilities ISR;       // NOTE: Mapped in PinAttributes!

        // Other capabilities:
        static PinCapabilities ADC;
        static PinCapabilities DAC;
        static PinCapabilities PWM;
        static PinCapabilities UART;

        // Each class of pins (e.g. GPIO, etc) should have their own 'capability'. This ensures that we
        // can compare classes of pins along with their properties by just looking at the capabilities.
        static PinCapabilities Native;
        static PinCapabilities I2S;
        static PinCapabilities Error;
        static PinCapabilities Void;

        inline PinCapabilities operator|(PinCapabilities rhs) { return PinCapabilities(_value | rhs._value); }
        inline PinCapabilities operator&(PinCapabilities rhs) { return PinCapabilities(_value & rhs._value); }
        inline bool            operator==(PinCapabilities rhs) const { return _value == rhs._value; }
        inline bool            operator!=(PinCapabilities rhs) const { return _value != rhs._value; }

        inline operator bool() { return _value != 0; }

        inline bool has(PinCapabilities t) { return (*this & t) == t; }
    };
}
