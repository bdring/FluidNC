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

#include "PinCapabilities.h"

namespace Pins {
    PinCapabilities PinCapabilities::None(0);

    // Use a little trick here to ensure we don't make mistakes...
    // Do NOT add empty lines here, or have more than 32 items!!!
    // If we need more, we need a second uint32_t.
    //
    // The capabilities that have a mapped attribute have to be at
    // the top of the list. Note that this list must match the list
    // in attributes _exactly_!

    const int       START_LINE = __LINE__ + 1;
    PinCapabilities PinCapabilities::Input(1 << (__LINE__ - START_LINE));     // NOTE: Mapped in PinAttributes!
    PinCapabilities PinCapabilities::Output(1 << (__LINE__ - START_LINE));    // NOTE: Mapped in PinAttributes!
    PinCapabilities PinCapabilities::PullUp(1 << (__LINE__ - START_LINE));    // NOTE: Mapped in PinAttributes!
    PinCapabilities PinCapabilities::PullDown(1 << (__LINE__ - START_LINE));  // NOTE: Mapped in PinAttributes!
    PinCapabilities PinCapabilities::ISR(1 << (__LINE__ - START_LINE));       // NOTE: Mapped in PinAttributes!

    PinCapabilities PinCapabilities::PWM(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::UART(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::ADC(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::DAC(1 << (__LINE__ - START_LINE));

    PinCapabilities PinCapabilities::Native(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::I2S(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::Error(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::Void(1 << (__LINE__ - START_LINE));
}
