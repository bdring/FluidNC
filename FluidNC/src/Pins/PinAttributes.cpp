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

#include "PinAttributes.h"
#include "PinCapabilities.h"

namespace Pins {
    PinAttributes PinAttributes::Undefined(0);
    PinAttributes PinAttributes::None(0);

    // The attributes that have a mapped capability have to be at
    // the top of the list. Note that this list must match the list
    // in capabilities _exactly_!

    const int     START_LINE = __LINE__ + 1;
    PinAttributes PinAttributes::Input(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::Output(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::PullUp(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::PullDown(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::ISR(1 << (__LINE__ - START_LINE));     // ^        These are capabilities mapped
    const int     capabilityMask = (1 << (__LINE__ - START_LINE)) - 1;  // -------- Mask capabilities till here
    PinAttributes PinAttributes::ActiveLow(1 << (__LINE__ - START_LINE));
    PinAttributes PinAttributes::Exclusive(1 << (__LINE__ - START_LINE));  // \/       These are attributes
    PinAttributes PinAttributes::InitialOn(1 << (__LINE__ - START_LINE));  // \/       These are attributes

    bool PinAttributes::validateWith(PinCapabilities caps) {
        auto capMask  = (caps._value & capabilityMask);
        auto attrMask = (_value & capabilityMask);

        // We're good if:
        return (capMask & attrMask) == attrMask &&  // the capabilities overlap with the required attributes AND
               (capMask | attrMask) == capMask;     // if the required attributes all match an available capability
    }

    bool PinAttributes::conflictsWith(PinAttributes t) {
        // Input and output are mutually exclusive:
        if (t.has(Input) && t.has(Output)) {
            return true;
        }

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
