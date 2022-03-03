// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinExtenderDriver.h"
#include "../Configuration/Configurable.h"
#include "../Machine/MachineConfig.h"
#include "../Machine/I2CBus.h"
#include "../Platform.h"

#include <atomic>

namespace Pins {
    class PCA9539PinDetail;
}

namespace Extenders {
    enum class I2CExtenderDevice {
        Unknown,
        PCA9539,
    };

    // Pin extenders...
    //
    // The PCA9539 is identical to the PCA9555 in terms of API. It provides 2 address
    // pins, so a maximum of 4 possible values. Per PCA, there are 16 I/O ports in 2
    // separate registers, so that's a total of 16*4 = 64 values.
    // Datasheet: https://www.ti.com/lit/ds/symlink/pca9539.pdf
    // Speed: 400 kHz
    //
    // The PCA8574 is quite similar as well, but only has 8 bits per device, so a single
    // register. It has 3 address pins, so 8 possible values. 8*8=64 bits.
    // Datasheet: https://www.nxp.com/docs/en/data-sheet/PCA8574_PCA8574A.pdf
    // Speed: 400 kHz
    //
    // An optional 'interrupt' line can be used. When the 'interrupt' is called, it means
    // that *some* pin has changed state. We don't know which one that was obviously.
    // However, we can then query the individual pins (thereby resetting them) and throwing
    // the results as individual ISR's.
    //
    // NOTE: The data sheet explains that interrupts can be chained. If that is the case, the
    // interrupt will have the effect that ALL PCA's in the chain have to be queried. Needless
    // to say, this is usually a bad idea, because things like endstops become much slower
    // as a result. For now, I just felt like not supporting it.
    //
    // The MCP23017 has two interrupt lines, one for register A and register B. Apart from
    // that it appears to be quite similar as well. It has 3 address lines and 16 I/O ports,
    // so that's a total of 8 * 16 = 128 I/O ports.
    // Datasheet: https://ww1.microchip.com/downloads/en/devicedoc/20001952c.pdf
    // Speed: 100 kHz, 400 kHz, 1.7 MHz.
    //
    // MCP23S17 is similar to MCP23017 but works using SPI instead of I2C (10 MHz). MCP23S08
    // seems to be the same, but 8-bit.
    //
    // MAX7301 is SPI based, and like all the others, it can generate an ISR when the state
    // changes (pin 31). Address is selected like any other SPI device by CS. MAX7301 includes
    // pullups and schmitt triggers.
    // Datasheet: https://datasheet.lcsc.com/lcsc/1804140032_Maxim-Integrated-MAX7301AAX-_C143583.pdf
    //
    // How this class works...
    //
    // A single device has a bunch of pins, normally 8 or 16. These devices also have an address.
    // The maximum number of extender I/O pins that we currently support is currently 64. Note that
    // the I2C frequency doesn't really matter if you have the ISR handled. We limit the maximum number
    // of ports to 64 here, which basically means you *can* wire multiple devices on a single ISR line
    // by making good use of the addresses.
    //
    // Keep in mind that low latency is only possible if you do things correctly with placement on the
    // PCB, and with designating I/O ports for very specific purposes. If not, the firmware won't know
    // what an interrupt means, and has to figure it out before it can take action.
    //
    // A typical configuration looks like:
    //
    //   device: pca9539
    //   device_id: 0
    //   interrupt: gpio.36
    //
    class I2CExtender : public PinExtenderDriver {
        struct RegisterSet {
            union {
                uint64_t value = 0;
                uint8_t  bytes[8];
            };
        };

        struct ISRData {
            using ISRCallback = void (*)(void*);
            ISRData() : callback(nullptr), data(nullptr) {}

            ISRCallback callback;
            void*       data;

            inline bool defined() const { return callback != nullptr; }
        };

        // Device info:
        int _device   = int(I2CExtenderDevice::Unknown);
        int _deviceId = 0;

        static const int TaskDelayBetweenIterations = 10;

        int errorCount = 0;

        // Operation and status work together and form a common bitmask. Operation is just not reset,
        // while status is.
        uint8_t _operation = 0;

        // This information is filled based on the "device" and "device_id" during initialization:
        uint8_t _bus          = 0;
        uint8_t _address      = 0x74;
        uint8_t _ports        = 16;
        uint8_t _invertReg    = 0xFF;
        uint8_t _operationReg = 0xFF;
        uint8_t _inputReg     = 0xFF;
        uint8_t _outputReg    = 0xFF;
        Pin     _interruptPin;

        RegisterSet _claimed;

        Machine::I2CBus* _i2cBus;

        uint8_t I2CGetValue(uint8_t address, uint8_t reg);
        void    I2CSetValue(uint8_t address, uint8_t reg, uint8_t value);

        // Current register values:
        RegisterSet          _configuration;
        RegisterSet          _invert;
        volatile RegisterSet _input;
        volatile RegisterSet _output;

        // I2C communications within an ISR is not a good idea, it will crash everything. We offload
        // the communications using a task queue. Dirty tells which devices and registers to poll.
        // Every I2C roundtrip is always responsible for 8 bytes.
        TaskHandle_t _isrHandler = nullptr;

        uint8_t              _usedIORegisters;
        std::atomic<uint8_t> _dirtyWriteBuffer;
        std::atomic<uint8_t> _dirtyWrite;

        // Status is a bitmask that tells the task handle what needs to happen during the next roundtrip.
        // This works together with 'operation'.
        std::atomic<uint8_t> _status;
        ISRData              _isrData[64];

        static void isrTaskLoop(void* arg);
        void        isrTaskLoopDetail();

        static void interruptHandler(void* arg);

        void IOError();

    public:
        I2CExtender();

        void claim(pinnum_t index) override;
        void free(pinnum_t index) override;

        void group(Configuration::HandlerBase& handler) override;
        void validate() const override;
        void init();

        void IRAM_ATTR setupPin(pinnum_t index, Pins::PinAttributes attr) override;
        void IRAM_ATTR writePin(pinnum_t index, bool high) override;
        bool IRAM_ATTR readPin(pinnum_t index) override;
        void IRAM_ATTR flushWrites() override;

        void attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt(pinnum_t index) override;

        const char* name() const override;

        ~I2CExtender();
    };
}
