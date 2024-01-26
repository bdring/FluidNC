// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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
        PinCapabilities(const PinCapabilities&)            = default;
        PinCapabilities& operator=(const PinCapabilities&) = default;

        // All the capabilities we use and test:
        static PinCapabilities None;      // Nonexistent pin
        static PinCapabilities Reserved;  // Pin reserved for system use

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
        static PinCapabilities UARTIO;
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
