#include "../TestFramework.h"

#include <src/Pin.h>
#include <src/PinMapper.h>

#ifdef ESP32
extern "C" void __pinMode(uint8_t pin, uint8_t mode);
extern "C" int  __digitalRead(uint8_t pin);
extern "C" void __digitalWrite(uint8_t pin, uint8_t val);

struct GPIONative {
    inline static void initialize() {
        for (int i = 16; i <= 17; ++i) {
            __pinMode(i, OUTPUT);
            __digitalWrite(i, LOW);
        }
    }
    inline static void mode(int pin, uint8_t mode) { __pinMode(pin, mode); }
    inline static void write(int pin, bool val) { __digitalWrite(pin, val ? HIGH : LOW); }
    inline static bool read(int pin) { return __digitalRead(pin) != LOW; }
};
#else
#    include <SoftwareGPIO.h>

struct GPIONative {
    // We test GPIO pin 16 and 17, and GPIO 16 is wired directly to 17:
    static void WriteVirtualCircuitHystesis(SoftwarePin* pins, int pin, bool value) {
        switch (pin) {
            case 16:
            case 17:
                pins[16].handlePadChange(value);
                pins[17].handlePadChange(value);
                break;
        }
    }

    inline static void initialize() { SoftwareGPIO::instance().reset(WriteVirtualCircuitHystesis, false); }
    inline static void mode(int pin, uint8_t mode) { SoftwareGPIO::instance().setMode(pin, mode); }
    inline static void write(int pin, bool val) { SoftwareGPIO::instance().writeOutput(pin, val); }
    inline static bool read(int pin) { return SoftwareGPIO::instance().read(pin); }
};

void digitalWrite(uint8_t pin, uint8_t val);
void pinMode(uint8_t pin, uint8_t mode);
int  digitalRead(uint8_t pin);

#endif

namespace Pins {
    Test(GPIO, BasicInputOutput1) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Pin gpio17 = Pin::create("gpio.17");

        gpio16.setAttr(Pin::Attr::Output);
        gpio17.setAttr(Pin::Attr::Input);

        Assert(false == gpio16.read());
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));

        gpio16.on();

        Assert(true == gpio16.read());
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));

        gpio16.off();

        Assert(false == gpio16.read());
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));
    }

    Test(GPIO, BasicInputOutput2) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Pin gpio17 = Pin::create("gpio.17");

        gpio16.setAttr(Pin::Attr::Input);
        gpio17.setAttr(Pin::Attr::Output);

        Assert(false == gpio16.read());
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));

        gpio17.on();

        Assert(true == gpio16.read());
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));

        gpio17.off();

        Assert(false == gpio16.read());
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));
    }

    void TestISR(int deltaRising, int deltaFalling, int mode) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Pin gpio17 = Pin::create("gpio.17");

        gpio16.setAttr(Pin::Attr::Input | Pin::Attr::ISR);
        gpio17.setAttr(Pin::Attr::Output);

        // Two ways to set I/O:
        // 1. using on/off
        // 2. external source (e.g. set softwareio pin value)
        //
        // We read as well, because that shouldn't modify the state.
        //
        // NOTE: Hysteresis tells us that we get changes a lot during a small
        // window in time. Unfortunately, it's practically impossible to test
        // because it bounces all over the place... TODO FIXME, some mechanism
        // to cope with that.

        for (int i = 0; i < 10; ++i) {
            if (deltaRising) {
                auto oldCount = hitCount;
                gpio17.on();
                delay(1);
                auto newCount = hitCount;

                Assert(oldCount < newCount, "Expected rise after set state");
            } else {
                gpio17.on();
            }

            if (deltaFalling) {
                auto oldCount = hitCount;
                gpio17.off();
                delay(1);
                auto newCount = hitCount;

                Assert(oldCount < newCount, "Expected rise after set state");
            } else {
                gpio17.off();
            }
        }

        // Detach interrupt. Regardless of what we do, it shouldn't change hitcount anymore.
        gpio16.detachInterrupt();

        auto oldCount = hitCount;
        gpio17.on();
        gpio17.off();
        delay(1);
        auto newCount = hitCount;

        Assert(oldCount == newCount, "ISR hitcount error");
    }

    Test(GPIO, ISRRisingPin) { TestISR(1, 0, RISING); }

    Test(GPIO, ISRFallingPin) { TestISR(0, 1, FALLING); }

    Test(GPIO, ISRChangePin) { TestISR(1, 1, CHANGE); }

    Test(GPIO, NativeForwardingInput) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Pin gpio17 = Pin::create("gpio.17");

        PinMapper map1(gpio16);
        PinMapper map2(gpio17);

        pinMode(map1.pinId(), INPUT);
        gpio17.setAttr(Pin::Attr::Output);

        Assert(LOW == digitalRead(map1.pinId()));
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));

        gpio17.on();

        Assert(HIGH == digitalRead(map1.pinId()));
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));

        gpio17.off();

        Assert(LOW == digitalRead(map1.pinId()));
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));
    }

    Test(GPIO, NativeForwardingOutput) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Pin gpio17 = Pin::create("gpio.17");

        PinMapper map1(gpio16);
        PinMapper map2(gpio17);

        pinMode(map1.pinId(), OUTPUT);
        gpio17.setAttr(Pin::Attr::Input);

        digitalWrite(map1.pinId(), LOW);
        Assert(LOW == digitalRead(map1.pinId()));
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));

        digitalWrite(map1.pinId(), HIGH);

        Assert(HIGH == digitalRead(map1.pinId()));
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));

        digitalWrite(map1.pinId(), LOW);

        Assert(LOW == digitalRead(map1.pinId()));
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));
    }

    Test(GPIO, Name) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16");
        Assert(gpio16.name().equals("gpio.16"), "Name is %s", gpio16.name().c_str());
    }

    Test(GPIO, NameCaseSensitivity) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("GpIo.16");
        Assert(gpio16.name().equals("gpio.16"), "Name is %s", gpio16.name().c_str());
    }

    Test(GPIO, ActiveLow) {
        GPIONative::initialize();

        Pin gpio16 = Pin::create("gpio.16:low");
        Pin gpio17 = Pin::create("gpio.17");

        gpio16.setAttr(Pin::Attr::Output);
        gpio17.setAttr(Pin::Attr::Input);

        Assert(false == gpio16.read());
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));

        gpio16.on();

        Assert(true == gpio16.read());
        Assert(false == gpio17.read());
        Assert(false == GPIONative::read(16));
        Assert(false == GPIONative::read(17));

        gpio16.off();

        Assert(false == gpio16.read());
        Assert(true == gpio17.read());
        Assert(true == GPIONative::read(16));
        Assert(true == GPIONative::read(17));
    }

    class GPIOISR {
        int  hitCount;
        void HandleISR() { ++hitCount; }

    public:
        GPIOISR(int deltaRising, int deltaFalling, int mode) {
            GPIONative::initialize();

            Pin gpio16 = Pin::create("gpio.16");
            Pin gpio17 = Pin::create("gpio.17");

            gpio16.setAttr(Pin::Attr::Input | Pin::Attr::ISR);
            gpio17.setAttr(Pin::Attr::Output);

            // Two ways to set I/O:
            // 1. using on/off
            // 2. external source (e.g. set softwareio pin value)
            //
            // We read as well, because that shouldn't modify the state.
            //
            // NOTE: Hysteresis tells us that we get changes a lot during a small
            // window in time. Unfortunately, it's practically impossible to test
            // because it bounces all over the place... TODO FIXME, some mechanism
            // to cope with that.

            for (int i = 0; i < 10; ++i) {
                if (deltaRising) {
                    auto oldCount = hitCount;
                    gpio17.on();
                    delay(1);
                    auto newCount = hitCount;

                    Assert(oldCount < newCount, "Expected rise after set state");
                } else {
                    gpio17.on();
                }

                if (deltaFalling) {
                    auto oldCount = hitCount;
                    gpio17.off();
                    delay(1);
                    auto newCount = hitCount;

                    Assert(oldCount < newCount, "Expected rise after set state");
                } else {
                    gpio17.off();
                }
            }

            // Detach interrupt. Regardless of what we do, it shouldn't change hitcount anymore.
            gpio16.detachInterrupt();

            auto oldCount = hitCount;
            gpio17.on();
            gpio17.off();
            delay(1);
            auto newCount = hitCount;

            Assert(oldCount == newCount, "ISR hitcount error");
        }
    };

    Test(GPIO, ISRRisingPinClass) { GPIOISR isr(1, 0, RISING); }

    Test(GPIO, ISRFallingPinClass) { GPIOISR isr(0, 1, FALLING); }

    Test(GPIO, ISRChangePinClass) { GPIOISR isr(1, 1, CHANGE); }
}
