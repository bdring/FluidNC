// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PinAttributes.h"

namespace Pins {
    PinAttributes PinAttributes::Undefined(0);
    PinAttributes PinAttributes::None(0);
    PinAttributes PinAttributes::Reserved(1);

    // The attributes that have a mapped capability have to be at
    // the top of the list. Note that this list must match the list
    // in capabilities _exactly_!

    const int     START_LINE = __LINE__ + 1;
    PinAttributes PinAttributes::Input(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::Output(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::PullUp(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::PullDown(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::ISR(1 << (__LINE__ - START_LINE));  // ^        These are capabilities mapped

    PinAttributes PinAttributes::PWM(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::UART(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::ADC(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::DAC(1 << (__LINE__ - START_LINE));

    const int     capabilityMask = (1 << (__LINE__ - START_LINE)) - 1;  // -------- Mask capabilities till here
    PinAttributes PinAttributes::ActiveLow(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::Exclusive(1 << (__LINE__ - START_LINE));  // \/       These are attributes
    PinAttributes PinAttributes::InitialOn(1 << (__LINE__ - START_LINE));  // \/       These are attributes

    PinAttributes PinAttributes::DS0(1 << (__LINE__ - START_LINE));  // Lowest drive strength
    PinAttributes PinAttributes::DS1(1 << (__LINE__ - START_LINE));  // Second to lowest drive strength
    PinAttributes PinAttributes::DS2(1 << (__LINE__ - START_LINE));  // Second to highest drive strength
    PinAttributes PinAttributes::DS3(1 << (__LINE__ - START_LINE));  // Highest drive strength

    // cppcheck-suppress unusedFunction
    bool PinAttributes::validateWith(PinCapabilities caps) {
        auto capMask  = (caps._value & capabilityMask);
        auto attrMask = (_value & capabilityMask);

        // We're good if:
        return (capMask & attrMask) == attrMask &&  // the capabilities overlap with the required attributes AND
               (capMask | attrMask) == capMask;     // if the required attributes all match an available capability
    }

    // cppcheck-suppress unusedFunction
    bool PinAttributes::conflictsWith(PinAttributes t) {
        // If it's exclusive, we are not allowed to set it again:
        if (_value != Undefined._value && this->has(Exclusive) && _value != t._value) {
            return true;
        }

        // If we have an ISR that doesn't have an Input, that doesn't make much sense.
        if (t.has(ISR) && !t.has(Input)) {
            return true;
        }

        // Otherwise, all is good, and we can set it.
        return false;
    }
}
