// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinCapabilities.h"

namespace Pins {
    PinCapabilities PinCapabilities::None(0);
    PinCapabilities PinCapabilities::Reserved(1);

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
    PinCapabilities PinCapabilities::UARTIO(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::Error(1 << (__LINE__ - START_LINE));
    PinCapabilities PinCapabilities::Void(1 << (__LINE__ - START_LINE));
}
