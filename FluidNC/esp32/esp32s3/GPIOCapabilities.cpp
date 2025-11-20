#include "Pins/GPIOPinDetail.h"

namespace Pins {
    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        if ((index <= 21) || (index >= 35 && index <= 48)) {
            return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                   PinCapabilities::PullDown | PinCapabilities::PWM | PinCapabilities::ISR | PinCapabilities::UART |
                   (index <= 20 ? PinCapabilities::ADC : PinCapabilities::None);
        } else {
            // Not mapped to actual GPIO pins
            return PinCapabilities::None;
        }
    }
}
