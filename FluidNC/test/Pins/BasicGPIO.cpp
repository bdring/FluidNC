#include "TestFramework.h"

#include <Arduino.h>

#ifdef ESP32

extern "C" int  __digitalRead(uint8_t pin);
extern "C" void __pinMode(uint8_t pin, uint8_t mode);
extern "C" void __digitalWrite(uint8_t pin, uint8_t val);

namespace Pins {
    Test(BasicGPIO, ReadGPIORaw) {
        auto pin = 26;

        // Enable driver, write high/low.
        __pinMode(pin, OUTPUT);

        __digitalWrite(pin, HIGH);
        auto value = __digitalRead(pin);
        Assert(value != 0);

        __digitalWrite(pin, LOW);
        value = __digitalRead(pin);
        Assert(value == 0);

        __digitalWrite(pin, HIGH);
        value = __digitalRead(pin);
        Assert(value != 0);

        __digitalWrite(pin, LOW);
        value = __digitalRead(pin);
        Assert(value == 0);

        // Disable driver, should read the last value (low).
        __pinMode(pin, INPUT);
        value = __digitalRead(pin);
        Assert(value == 0);
    }
}

#endif
