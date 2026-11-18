#include "Pins/GPIOPinDetail.h"

#include <soc/soc_caps.h>

namespace Pins {
    PinCapabilities GPIOPinDetail::GetDefaultCapabilities(pinnum_t index) {
        if (index >= SOC_GPIO_PIN_COUNT) {
            return PinCapabilities::None;
        }

        // GPIO24/25 are USB0 D-/D+; GPIO26/27 are USB1 D-/D+.
        if (index >= 24 && index <= 27) {
            return PinCapabilities::None;
        }

        // The in-package flash/PSRAM uses dedicated MSPI pads, not GPIOs.

        return PinCapabilities::Native | PinCapabilities::Input | PinCapabilities::Output | PinCapabilities::PullUp |
               PinCapabilities::PullDown | PinCapabilities::PWM | PinCapabilities::ISR | PinCapabilities::UART;
    }
}
