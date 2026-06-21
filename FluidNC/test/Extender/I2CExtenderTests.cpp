#include "../TestFramework.h"

#include <src/Pin.h>
#include <src/PinMapper.h>
#include <src/Machine/I2CBus.h>
#include <src/Machine/MachineConfig.h>
#include <src/Extenders/I2CExtender.h>
#include <Wire.h>

#include "Capture.h"

#include <SoftwareGPIO.h>
#include <mutex>

namespace {
    struct GPIONative {
        // We wire 15 to 20.
        static void WriteVirtualCircuitHystesis(SoftwarePin* pins, int pin, bool value) {
            // switch (pin) {
            //     case 20:
            //         pins[15].handlePadChange(value);
            //         break;
            // }
        }

        inline static void initialize() { SoftwareGPIO::instance().reset(WriteVirtualCircuitHystesis, true); }
        inline static void mode(int pin, uint8_t mode) { SoftwareGPIO::instance().setMode(pin, mode); }
        inline static void write(int pin, bool val) { SoftwareGPIO::instance().writeOutput(pin, val); }
        inline static bool read(int pin) { return SoftwareGPIO::instance().read(pin); }
    };

    class PCA9539Emulator {
        uint8_t reg_config[2];
        uint8_t reg_invert[2];
        uint8_t reg_input[2];
        uint8_t reg_output[2];
        uint8_t pad_value[2];  // input values

        uint8_t accessedRegisters = 0;
        uint8_t previousRegisters = 0;  // For debugging purposes.

        int isrPin_;

        void setRegister(int reg, uint8_t value) {
            accessedRegisters |= uint8_t(1 << reg);
            switch (reg) {
                // input
                case 2:
                    reg_output[0] = value;
                    break;
                case 3:
                    reg_output[1] = value;
                    break;
                    // invert
                case 4:
                    reg_invert[0] = value;
                    reg_input[0]  = reg_invert[0] ^ pad_value[0];
                    break;
                case 5:
                    reg_invert[1] = value;
                    reg_input[1]  = reg_invert[1] ^ pad_value[1];
                    break;
                    // config
                case 6:
                    reg_config[0] = value;
                    break;
                case 7:
                    reg_config[1] = value;
                    break;
                default:
                    Assert(false, "Not supported");
                    break;
            }
        }

        void handler(TwoWire* theWire, std::vector<uint8_t>& data) {
            if (data.size() == 1) {
                if (data[0] == 0 || data[0] == 1) {
                    accessedRegisters |= uint8_t(1 << data[0]);
                    Assert(theWire->SendSize() == 0);
                    theWire->Send(uint8_t(reg_input[data[0]]));
                    data.clear();

                    // Clear ISR:
                    if (isrPin_ >= 0) {
                        GPIONative::write(isrPin_, true);
                    }
                } else if (data[0] >= 2 && data[0] <= 7) {
                    // ignore until next roundtrip
                } else {
                    Assert(false, "Unknown register");
                }
            } else if (data.size() == 2) {
                if (data[0] >= 2 && data[0] <= 7) {
                    setRegister(data[0], data[1]);
                    data.clear();
                } else {
                    Assert(false, "Unknown register");
                }
            } else {
                Assert(false, "Unknown size");
            }
        }

    public:
        PCA9539Emulator(int isrPin) : isrPin_(isrPin) {
            for (int i = 0; i < 2; ++i) {
                reg_config[i] = 0;
                reg_invert[i] = 0;
                reg_input[i]  = 0;
                reg_output[i] = 0;
                pad_value[i]  = 0;
            }

            if (isrPin_ >= 0) {
                GPIONative::write(isrPin_, true);
            }
        }

        static void wireResponseHandler(TwoWire* theWire, std::vector<uint8_t>& data, void* userData) {
            static_cast<PCA9539Emulator*>(userData)->handler(theWire, data);
        }

        void setPadValue(int pinId, bool v) {
            uint8_t mask = uint8_t(1 << (pinId % 8));
            int     idx  = pinId / 8;

            if (reg_config[idx] & mask)  // input
            {
                auto oldValue = pad_value[idx] & mask;
                auto newValue = v ? mask : uint8_t(0);

                if (oldValue != newValue) {
                    pad_value[idx] = (pad_value[idx] & ~mask) | newValue;
                    reg_input[idx] = reg_invert[idx] ^ pad_value[idx];

                    // Trigger ISR on 'falling'.
                    if (isrPin_ >= 0) {
                        GPIONative::write(isrPin_, false);
                    }
                }
            }
        }

        bool getPadValue(int pinId) {
            uint8_t mask = uint8_t(1 << (pinId % 8));
            int     idx  = pinId / 8;

            if ((reg_config[idx] & mask) == 0) {
                // This is an output pin, so combine registers:
                return ((reg_output[idx] ^ reg_invert[idx]) & mask) != 0;
            } else {
                // This is an input pin, so use the pad_value
                return (pad_value[idx] & mask) != 0;
            }
        }

        uint8_t registersUsed() {
            auto result       = accessedRegisters;
            previousRegisters = result;
            accessedRegisters = 0;
            return result;
        }
    };

    class Roundtrip {
        uint32_t before;

    public:
        Roundtrip() { before = Capture::instance().current(); }

        ~Roundtrip() {
            for (int i = 0; i < 3; ++i) {
                while (Capture::instance().current() < before + 1) {
                    delay(10);
                }
                before = Capture::instance().current();
            }
        }
    };

    std::mutex single_thread;
}

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
        std::lock_guard<std::mutex> guard(single_thread);

        PCA9539Emulator pca(-1);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();
    }

    Test(I2CExtender, ClaimRelease) {
        std::lock_guard<std::mutex> guard(single_thread);
        PCA9539Emulator             pca(-1);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

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

    Test(I2CExtender, ExtenderNoInterrupt) {
        std::lock_guard<std::mutex> guard(single_thread);
        PCA9539Emulator             pca(-1);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        {
            // Setup will trigger some events on I2C: 'config', 'invert', 'write', 'read'.

            i2c.claim(0);
            i2c.setupPin(0, Pin::Attr::Output);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            // Check PCA values:
            Assert(pca.registersUsed() == 0x55, "Expected invert, config, write, read bytes being used");
            Assert(!pca.getPadValue(0));
        }

        // Read will trigger an update, because we don't have an ISR
        {
            bool readPin = i2c.readPin(0);
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x01, "Expected roundtrip for read / no ISR");
            Assert(readPin == false, "Expected 'true' on pin");
        }

        // Test write pin:
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x04, "Expected roundtrip for write / no ISR");
            Assert(pca.getPadValue(0), "Expected pad to be 'true'");
        }
        {
            // Write to set it 'low'.
            i2c.writePin(0, false);
            i2c.flushWrites();
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x04, "Expected roundtrip for write / no ISR");
            Assert(!pca.getPadValue(0), "Expected pad to be 'false'");
        }
        {
            // Write to set it 'low'. It's already low, no-op.
            i2c.writePin(0, false);
            i2c.flushWrites();
            // no-op.
            Assert(pca.registersUsed() == 0x00, "Expected roundtrip for write / no ISR");
            Assert(!pca.getPadValue(0), "Expected pad to be 'false'");
        }
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            auto recv = Wire.Receive();

            Assert(pca.registersUsed() == 0x04, "Expected roundtrip for write / no ISR");
            Assert(pca.getPadValue(0), "Expected pad to be 'false'");
        }

        // NOTE: We ended with setting pin #0 to 'high' = 0x01

        // Setup pin for reading:
        {
            i2c.claim(1);
            i2c.setupPin(1, Pin::Attr::Input);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x55, "Expected invert, config, write, read bytes being used");
            Assert(pca.getPadValue(0));
            Assert(!pca.getPadValue(1));
        }

        // Setup another pin for reading with an invert mask and a PU:
        {
            i2c.claim(2);
            i2c.setupPin(2, Pin::Attr::Input | Pin::Attr::ActiveLow | Pin::Attr::PullUp);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x55, "Expected invert, config, write, read bytes being used");
            Assert(pca.getPadValue(0));
            Assert(!pca.getPadValue(1));
            Assert(!pca.getPadValue(2));
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(1);
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x01, "Expected invert, config, write, read bytes being used");
            Assert(readPin == false);
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(2);
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x01, "Expected invert, config, write, read bytes being used");
            Assert(readPin == true, "Expected 'true' on pin");
        }

        pca.setPadValue(1, true);
        pca.setPadValue(2, true);

        // Test read pin:
        {
            bool readPin = i2c.readPin(1);
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x01, "Expected invert, config, write, read bytes being used");
            Assert(readPin == true);
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(2);
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x01, "Expected invert, config, write, read bytes being used");
            Assert(readPin == false, "Expected 'true' on pin");
        }
    }

    Test(I2CExtender, ExtenderWithInterrupt) {
        std::lock_guard<std::mutex> guard(single_thread);
        GPIONative::initialize();
        PCA9539Emulator pca(15);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender with ISR on gpio.15
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(true);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        {
            i2c.claim(0);
            i2c.setupPin(0, Pin::Attr::Output);
            { Roundtrip rt; }

            Assert(pca.registersUsed() == 0x55, "Expected invert, config, write, read bytes being used");
        }

        // Read will NOT trigger an update because we have an ISR to tell us when it changes:
        {
            bool readPin = i2c.readPin(0);
            Assert(readPin == false, "Expected 'false' on pin");
            Assert(pca.registersUsed() == 0, "Expected no-op for read");
        }

        // Test write pin:
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x04, "Expected no-op for read");
        }
        {
            // Write to set it 'low'.
            i2c.writePin(0, false);
            i2c.flushWrites();
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x04);
        }
        {
            // Write to set it 'low'. It's already low, no-op.
            i2c.writePin(0, false);
            i2c.flushWrites();
            // no-op.

            Assert(pca.registersUsed() == 0, "Expected no-op");
        }
        {
            // Write to set it 'high'.
            i2c.writePin(0, true);
            i2c.flushWrites();
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x04);
        }

        // NOTE: We ended with setting pin #0 to 'high' = 0x01

        // Setup pin for reading:
        {
            i2c.claim(1);
            i2c.setupPin(1, Pin::Attr::Input);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x55);
        }

        // Setup another pin for reading with an invert mask and a PU:
        {
            i2c.claim(2);
            i2c.setupPin(2, Pin::Attr::Input | Pin::Attr::ActiveLow | Pin::Attr::PullUp);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x55);
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(1);
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x0);
            Assert(readPin == false, "Expected 'true' on pin");
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(2);
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x0);
            Assert(readPin == true, "Expected 'true' on pin");
        }

        // Trigger an ISR, change both pins
        {
            pca.setPadValue(1, true);
            pca.setPadValue(2, true);
            { Roundtrip rt; }
            Assert(pca.registersUsed() == 0x01);
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(1);
            Assert(pca.registersUsed() == 0x0);
            Assert(readPin == true, "Expected 'true' on pin");
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(2);
            Assert(pca.registersUsed() == 0x0);
            Assert(readPin == false, "Expected 'true' on pin");
        }
    }

    void HandleInterrupt(void* data) { ++(*reinterpret_cast<uint32_t*>(data)); }

    Test(I2CExtender, ISRTriggerWithInterrupt) {
        std::lock_guard<std::mutex> guard(single_thread);
        GPIONative::initialize();
        PCA9539Emulator pca(15);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(true);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        {
            i2c.claim(9);
            i2c.setupPin(9, Pin::Attr::Input | Pin::Attr::ISR);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        uint32_t isrCounter = 0;

        {
            i2c.attachInterrupt(9, HandleInterrupt, &isrCounter, CHANGE);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        { Roundtrip rt; }

        // Test read pin:
        {
            bool readPin = i2c.readPin(9);
            Assert(readPin == false, "Expected 'true' on pin");
            Assert(pca.registersUsed() == 0x00);
        }

        // Change state, wait till roundtrip
        {
            pca.setPadValue(9, true);
            { Roundtrip rt; }

            // Test if ISR update went correctly:
            Assert(isrCounter == 1);
            Assert(pca.registersUsed() == 0x03);

            // Test read pin:
            bool readPin = i2c.readPin(9);
            Assert(readPin == true, "Expected 'true' on pin");
            Assert(pca.registersUsed() == 0x00);
        }

        {
            i2c.detachInterrupt(9);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        // Change state, wait till roundtrip
        {
            pca.setPadValue(9, false);
            { Roundtrip rt; }

            // Test if ISR detach went correctly:
            Assert(isrCounter == 1);
            Assert(pca.registersUsed() == 0x03);
        }
    }

    Test(I2CExtender, ISRTriggerWithoutInterrupt) {
        std::lock_guard<std::mutex> guard(single_thread);
        GPIONative::initialize();
        PCA9539Emulator pca(15);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        {
            i2c.claim(9);
            i2c.setupPin(9, Pin::Attr::Input | Pin::Attr::ISR);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        uint32_t isrCounter = 0;

        {
            // From this point on, we just need to respond to wire requests
            i2c.attachInterrupt(9, HandleInterrupt, &isrCounter, CHANGE);
        }

        // Test read pin:
        {
            bool readPin = i2c.readPin(9);
            Assert(readPin == false, "Expected 'true' on pin");
        }

        // Change state, wait till roundtrip
        {
            pca.setPadValue(9, true);

            { Roundtrip rt; }

            // Test if ISR update went correctly:
            Assert(isrCounter == 1);

            // Test read pin:
            bool readPin = i2c.readPin(9);
            Assert(readPin == true, "Expected 'true' on pin");
        }

        {
            pca.setPadValue(9, false);

            { Roundtrip rt; }

            // Test if ISR update went correctly:
            Assert(isrCounter == 2);

            // Test read pin:
            bool readPin2 = i2c.readPin(9);
            Assert(readPin2 == false, "Expected 'false' on pin");
        }

        {
            i2c.detachInterrupt(9);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        // Change state, wait till roundtrip
        {
            pca.setPadValue(9, false);
            { Roundtrip rt; }

            // Test if ISR detach went correctly:
            Assert(isrCounter == 2);
        }
    }

    void ReadInISRHandler(void* data) {
        auto i2c   = static_cast<Extenders::I2CExtender*>(data);
        auto value = i2c->readPin(9);
        Assert(value == true);
    }

    Test(I2CExtender, ReadInISR) {
        std::lock_guard<std::mutex> guard(single_thread);
        GPIONative::initialize();
        PCA9539Emulator pca(15);

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
        Wire.SetResponseHandler(PCA9539Emulator::wireResponseHandler, &pca);

        // Setup the extender
        Extenders::I2CExtender i2c;
        FakeInitHandler        fakeInit(false);
        i2c.group(fakeInit);
        i2c.validate();
        i2c.init();

        {
            i2c.claim(9);
            i2c.setupPin(9, Pin::Attr::Input | Pin::Attr::ISR);

            // Wait until synced (should be immediate after the thread gets some cpu) and check I2C comms:
            { Roundtrip rt; }

            auto regUsed = pca.registersUsed();
            Assert(regUsed >= 0xfd && regUsed <= 0xFF);
        }

        {
            pca.setPadValue(9, false);
            i2c.attachInterrupt(9, ReadInISRHandler, &i2c, CHANGE);
            pca.setPadValue(9, true);
            i2c.detachInterrupt(9);
            pca.setPadValue(9, false);
        }
    }
}
