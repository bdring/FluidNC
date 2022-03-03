#include "../TestFramework.h"

#include <src/Pin.h>
#include <src/Machine/I2CBus.h>
#include <src/Machine/MachineConfig.h>
#include <src/Extenders/I2CExtender.h>
#include <Wire.h>

#include "Capture.h"

namespace Configuration {
    Test(I2CExtender, I2CBasics) {
        // Initialize I2C bus
        Machine::I2CBus bus;
        bus._sda       = Pin::create("gpio.16");
        bus._scl       = Pin::create("gpio.17");
        bus._frequency = 100000;
        bus._busNumber = 0;

        bus.validate();
        bus.init();

        Wire.Clear();

        Assert(0 == bus.write(1, reinterpret_cast<const uint8_t*>("aap"), 3), "Bad write");
        auto data = Wire.Receive();

        Assert(data.size() == 3, "Expected 3 bytes");
        data.push_back(0);
        Assert(!strcmp(reinterpret_cast<const char*>(data.data()), "aap"), "Incorrect data read");

        uint8_t tmp[4];
        tmp[3] = 0;
        Assert(bus.read(1, tmp, 3) == 0, "Expected no data available for read");

        std::vector<uint8_t> tmp2;
        tmp2.push_back(uint8_t('p'));
        tmp2.push_back(uint8_t('i'));
        tmp2.push_back(uint8_t('m'));

        Wire.Send(tmp2);
        Assert(bus.read(1, tmp, 3) == 3, "Expected 3 bytes data available for read");
        Assert(bus.read(1, tmp, 3) == 0, "Expected no data available for read");
        Assert(!strcmp(reinterpret_cast<const char*>(tmp), "pim"), "Incorrect data read");
    }

    // Helper class for initialization of I2C extender
    class FakeInitHandler : public Configuration::HandlerBase {
        bool hasISR_;

    protected:
        void enterSection(const char* name, Configurable* value) override {}
        bool matchesUninitialized(const char* name) override { return true; }

    public:
        FakeInitHandler(bool hasISR) : hasISR_(hasISR) {}

        void item(const char* name, float& value, float minValue = -3e38, float maxValue = 3e38) override {}
        void item(const char* name, std::vector<speedEntry>& value) override {}
        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {}
        void item(const char* name, Pin& value) override {
            if (!strcmp(name, "interrupt") && hasISR_) {
                value = Pin::create("gpio.15");
            }
        }
        void item(const char* name, IPAddress& value) override {}
        void item(const char* name, int& value, EnumItem* e) override {
            if (!strcmp(name, "device")) {
                value = int(Extenders::I2CExtenderDevice::PCA9539);
            }
        }

        void item(const char* name, String& value, int minLength = 0, int maxLength = 255) override {}

        HandlerType handlerType() override { return HandlerType::Parser; }

        void item(const char* name, bool& value) override {}
        void item(const char* name, int32_t& value, int32_t minValue = 0, int32_t maxValue = INT32_MAX) override {
            if (!strcmp(name, "device_id")) {
                value = 0;
            }
        }
    };

    Test(I2CExtender, InitDeinit) {
        // Initialize I2C bus
        Machine::I2CBus bus;
        bus._sda       = Pin::create("gpio.16");
        bus._scl       = Pin::create("gpio.17");
        bus._frequency = 100000;
        bus._busNumber = 0;
        bus.init();

        // We need to set up the I2C config in the global 'config', or init of the extender will fail.
        Machine::MachineConfig mconfig;
        mconfig._i2c = &bus;
        config       = &mconfig;

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();
    }

    Test(I2CExtender, ClaimRelease) {
        // Initialize I2C bus
        Machine::I2CBus bus;
        bus._sda       = Pin::create("gpio.16");
        bus._scl       = Pin::create("gpio.17");
        bus._frequency = 100000;
        bus._busNumber = 0;
        bus.init();

        // We need to set up the I2C config in the global 'config', or init of the extender will fail.
        Machine::MachineConfig mconfig;
        mconfig._i2c = &bus;
        config       = &mconfig;

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        i2c.claim(1);
        i2c.claim(0);
        AssertThrow(i2c.claim(1));
        i2c.claim(2);
        AssertThrow(i2c.claim(64));
        AssertThrow(i2c.claim(-1));
        i2c.free(1);
        i2c.free(1);
        i2c.claim(1);
        AssertThrow(i2c.claim(1));
        i2c.free(0);
        i2c.free(1);
        i2c.free(2);
    }

    class Roundtrip {
        uint32_t before;

    public:
        Roundtrip() { before = Capture::instance().current(); }

        ~Roundtrip() {
            while (Capture::instance().current() < before + 1) {
                delay(10);
            }
        }
    };

    Test(I2CExtender, SetupPin) {
        // Initialize I2C bus
        Machine::I2CBus bus;
        bus._sda       = Pin::create("gpio.16");
        bus._scl       = Pin::create("gpio.17");
        bus._frequency = 100000;
        bus._busNumber = 0;
        bus.init();

        // We need to set up the I2C config in the global 'config', or init of the extender will fail.
        Machine::MachineConfig mconfig;
        mconfig._i2c = &bus;
        config       = &mconfig;

        Wire.Clear();

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        // Expected register values (see datasheet):
        //
        // 4 invert
        // 1 = invert, 0 = normal
        //
        // 6 config
        // 1 = input, 0 = output
        //
        // 2 write
        // 1 = high, 0 = low
        //
        // 0 read
        // high = 1, low = 0

        {
            Roundtrip rt;

            // Setup will trigger some events on I2C: 'config', 'invert', 'write', 'read'.
            // We have to set the 'read' before setting up the pin, or the I2C comms are going to fail.
            // Let's just set it 'high':
            Wire.Send(0x01);

            i2c.claim(0);
            i2c.setupPin(0, Pin::Attr::Output);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
        }
        {
            auto buffer = Wire.Receive();
            Assert(buffer.size() == 3 * 2 + 1, "Expected invert, config, write, read bytes being sent");

            const uint8_t expected[7] = { uint8_t(0x04), uint8_t(0x00), uint8_t(0x06), uint8_t(0x00),
                                          uint8_t(0x02), uint8_t(0x00), uint8_t(0x00) };
            Assert(!memcmp(expected, buffer.data(), 7), "Didn't expect data");
        }

        // Read will trigger an update, because we don't have an ISR
        {
            Wire.Send(0x01);
            bool readPin = i2c.readPin(0);
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 1, "Expected single data request / response roundtrip");
            Assert(recv[0] == 0, "Expected read");
            Assert(readPin == true, "Expected 'true' on pin");
        }

        // Test write pin:
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 2, "Expected single data request / response roundtrip");
            Assert(recv[0] == 2, "Expected write reg 0");
            Assert(recv[1] == 1, "Expected write reg 0 = 0");
        }
        {
            // Write to set it 'low'.
            i2c.writePin(0, false);
            i2c.flushWrites();
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 2, "Expected single data request / response roundtrip");
            Assert(recv[0] == 2, "Expected write reg 0");
            Assert(recv[1] == 0, "Expected write reg 0 = 0");
        }
        {
            // Write to set it 'low'. It's already low, no-op.
            i2c.writePin(0, false);
            i2c.flushWrites();
            // no-op.
        }
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 2, "Expected single data request / response roundtrip");
            Assert(recv[0] == 2, "Expected write reg 0");
            Assert(recv[1] == 1, "Expected write reg 0 = 0");
        }

        // NOTE: We ended with setting pin #0 to 'high' = 0x01

        // Setup pin for reading:
        {
            Roundtrip rt;

            // Setup will trigger some events on I2C: 'config', 'invert', 'write', 'read'.
            // We have to set the 'read' before setting up the pin, or the I2C comms are going to fail.
            // Let's just set it 'high':
            Wire.Send(0x00);

            i2c.claim(1);
            i2c.setupPin(1, Pin::Attr::Input);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
        }
        {
            auto buffer = Wire.Receive();
            Assert(buffer.size() == 3 * 2 + 1, "Expected invert, config, write, read bytes being sent");

            const uint8_t expected[7] = { uint8_t(0x04), uint8_t(0x00), uint8_t(0x06), uint8_t(0x02),
                                          uint8_t(0x02), uint8_t(0x01), uint8_t(0x00) };
            Assert(!memcmp(expected, buffer.data(), 7), "Didn't expect data");
        }

        // Setup another pin for reading with an invert mask and a PU:
        {
            Roundtrip rt;

            // Setup will trigger some events on I2C: 'config', 'invert', 'write', 'read'.
            // We have to set the 'read' before setting up the pin, or the I2C comms are going to fail.
            // Let's just set it 'high':
            Wire.Send(0x04);

            i2c.claim(2);
            i2c.setupPin(2, Pin::Attr::Input | Pin::Attr::ActiveLow | Pin::Attr::PullUp);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
        }
        {
            auto buffer = Wire.Receive();
            Assert(buffer.size() == 3 * 2 + 1, "Expected invert, config, write, read bytes being sent");

            const uint8_t expected[7] = { uint8_t(0x04), uint8_t(0x04), uint8_t(0x06), uint8_t(0x06),
                                          uint8_t(0x02), uint8_t(0x05), uint8_t(0x00) };
            Assert(!memcmp(expected, buffer.data(), 7), "Didn't expect data");
        }

        // Test read pin:
        {
            Wire.Send(0x02);
            bool readPin = i2c.readPin(1);
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 1, "Expected single data request / response roundtrip");
            Assert(recv[0] == 0, "Expected read");
            Assert(readPin == true, "Expected 'true' on pin");
        }

        // Test read pin:
        {
            Wire.Send(0x02);
            bool readPin = i2c.readPin(2);
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(recv.size() == 1, "Expected single data request / response roundtrip");
            Assert(recv[0] == 0, "Expected read");
            Assert(readPin == false, "Expected 'true' on pin");
        }
    }
}
