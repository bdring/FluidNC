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

        Assert(errorPin.capabilities() == Pin::Capabilities::Error, "Incorrect caps");
    }
}
