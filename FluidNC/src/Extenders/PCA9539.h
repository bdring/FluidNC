// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PinExtenderDriver.h"
#include "../Configuration/Configurable.h"
#include "../Machine/MachineConfig.h"
#include "../Machine/I2CBus.h"
#include "../Platform.h"

#include <bitset>

namespace Pins {
    class PCA9539PinDetail;
}

namespace Extenders {
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
    class PCA9539 : public PinExtenderDriver {
        friend class Pins::PCA9539PinDetail;

        // Address can be set for up to 4 devices. Each device supports 16 pins.

        static const int numberPins = 16 * 4;
        uint64_t         _claimed;

        Machine::I2CBus* _i2cBus;

        static uint8_t IRAM_ATTR I2CGetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg);
        static void IRAM_ATTR I2CSetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg, uint8_t value);

        // Registers:
        // 4x16 = 64 bits. Fits perfectly into an uint64.
        uint64_t          _configuration = 0;
        uint64_t          _invert        = 0;
        volatile uint64_t _value         = 0;

        // 4 devices, 2 registers per device. 8 bits is enough:
        uint8_t _dirtyRegisters = 0;

        QueueHandle_t _isrQueue   = nullptr;
        TaskHandle_t  _isrHandler = nullptr;

        static void isrTaskLoop(void* arg);

        struct ISRData {
            ISRData() = default;

            Pin                _pin;
            PCA9539*           _container = nullptr;
            volatile uint16_t* _valueBase = nullptr;
            uint8_t            _address   = 0;

            typedef void (*ISRCallback)(void*);

            bool        _hasISR          = false;
            ISRCallback _isrCallback[16] = { 0 };
            void*       _isrArgument[16] = { 0 };
            int         _isrMode[16]     = { 0 };

            void IRAM_ATTR updateValueFromDevice();
        };

        ISRData     _isrData[4];
        static void IRAM_ATTR updatePCAState(void* ptr);

    public:
        PCA9539() = default;

        void claim(pinnum_t index) override;
        void free(pinnum_t index) override;

        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        void init();

        void IRAM_ATTR setupPin(pinnum_t index, Pins::PinAttributes attr) override;
        void IRAM_ATTR writePin(pinnum_t index, bool high) override;
        bool IRAM_ATTR readPin(pinnum_t index) override;
        void IRAM_ATTR flushWrites() override;

        void attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt(pinnum_t index) override;

        const char* name() const override;

        ~PCA9539();
    };
}
