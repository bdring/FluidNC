#include "Pins/GPIOPinDetail.h"

namespace Pins {
    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        // RP2040 has 30 GPIO pins (0-29), all with similar capabilities
        // See https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf
        if (index < 26) {
            // GPIO 0-25: All support basic I/O, PWM, interrupts
            return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output |
                   PinCapabilities::PullUp | PinCapabilities::PullDown | PinCapabilities::PWM |
                   PinCapabilities::ISR | PinCapabilities::UART;
        } else if (index <= 29) {
            // GPIO 26-29: Add ADC capability (ADC0-ADC3)
            return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output |
                   PinCapabilities::PullUp | PinCapabilities::PullDown | PinCapabilities::PWM |
                   PinCapabilities::ISR | PinCapabilities::UART | PinCapabilities::ADC;
        } else {
            // Invalid pin
            return PinCapabilities::None;
        }
    }
}
