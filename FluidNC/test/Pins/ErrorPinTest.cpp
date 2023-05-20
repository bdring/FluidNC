#include "../TestFramework.h"

#include <src/Pin.h>

namespace Pins {
    Test(Error, Pins) {
        // Error pins should throw whenever they are used.

        Pin errorPin = Pin::Error();

        AssertThrow(errorPin.write(true));
        AssertThrow(errorPin.read());

        errorPin.setAttr(Pin::Attr::None);

        AssertThrow(errorPin.write(true));
        AssertThrow(errorPin.read());

        AssertThrow(errorPin.attachInterrupt([](void* arg) {}, EITHER_EDGE));
        AssertThrow(errorPin.detachInterrupt());

        Assert(errorPin.capabilities() == Pin::Capabilities::Error, "Incorrect caps");
    }
}
