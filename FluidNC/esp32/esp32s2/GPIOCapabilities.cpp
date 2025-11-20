#include "Pins/GPIOPinDetail.h"

namespace Pins {
    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        switch (index) {
            case 43:  // TX pin of Serial0. Note that Serial0 also runs through the Pins framework!
                return PinCapabilities::Native | PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::UART |
                       PinCapabilities::ADC;

            case 44:  // RX pin of Serial0. Note that Serial0 also runs through the Pins framework!
                return PinCapabilities::Native | PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR |
                       PinCapabilities::UART | PinCapabilities::ADC;

            case 46:
                return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::ADC | PinCapabilities::ISR | PinCapabilities::UART;

            default:
                if ((index <= 21) || index == 26 || (index >= 33 && index <= 45)) {
                    return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
                           PinCapabilities::PullDown | PinCapabilities::PWM | PinCapabilities::ISR | PinCapabilities::UART |
                           (index <= 20 ? PinCapabilities::ADC : PinCapabilities::None);
                } else {
                    // Not mapped to actual GPIO pins
                    return PinCapabilities::None;
                }
        }
    }
}
