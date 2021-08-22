#include "../TestFramework.h"

#include <src/Pin.h>
#include <esp32-hal-gpio.h>  // CHANGE

namespace Pins {
    Test(Error, Pins) {
        // Error pins should throw whenever they are used.

        Pin errorPin = Pin::Error();

        AssertThrow(errorPin.write(true));
        AssertThrow(errorPin.read());

        errorPin.setAttr(Pin::Attr::None);

        AssertThrow(errorPin.write(true));
        AssertThrow(errorPin.read());

        AssertThrow(errorPin.attachInterrupt([](void* arg) {}, CHANGE));
        AssertThrow(errorPin.detachInterrupt());

        Assert(errorPin.capabilities() == Pin::Capabilities::Error, "Incorrect caps");
    }
}
