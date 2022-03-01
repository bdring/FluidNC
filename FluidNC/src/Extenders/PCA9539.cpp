// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Extenders.h"
#include "PCA9539.h"
#include "../Logging.h"

#include <esp32-hal-gpio.h>
#include <freertos/FreeRTOS.h>

namespace Extenders {
    void PCA9539::claim(pinnum_t index) {
        Assert(index >= 0 && index < 16 * 4, "PCA9539 IO index should be [0-63]; %d is out of range", index);

        uint64_t mask = uint64_t(1) << index;
        Assert((_claimed & mask) == 0, "PCA9539 IO port %d is already used.", index);

        _claimed |= mask;
    }

    void PCA9539::free(pinnum_t index) {
        uint64_t mask = uint64_t(1) << index;
        _claimed &= ~mask;
    }

    uint8_t PCA9539::I2CGetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg) {
        auto err = bus->write(address, &reg, 1);

        if (err) {
            // log_info("Error writing to i2c bus. Code: " << err);
            return 0;
        }

        uint8_t inputData;
        if (bus->read(address, &inputData, 1) != 1) {
            // log_info("Error reading from i2c bus.");
        }

        return inputData;
    }

    void PCA9539::I2CSetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg, uint8_t value) {
        uint8_t data[2];
        data[0]  = reg;
        data[1]  = uint8_t(value);
        auto err = bus->write(address, data, 2);

        if (err) {
            log_error("Error writing to i2c bus; PCA9539 failed. Code: " << err);
        }
    }

    void PCA9539::validate() const {
        auto i2c = config->_i2c;
        Assert(i2c != nullptr, "PCA9539 works through I2C, but I2C is not configured.");
    }

    void PCA9539::group(Configuration::HandlerBase& handler) {
        handler.item("interrupt0", _isrData[0]._pin);
        handler.item("interrupt1", _isrData[1]._pin);
        handler.item("interrupt2", _isrData[2]._pin);
        handler.item("interrupt3", _isrData[3]._pin);
    }

    void PCA9539::isrTaskLoop(void* arg) {
        auto inst = static_cast<PCA9539*>(arg);
        while (true) {
            void* ptr;
            if (xQueueReceive(inst->_isrQueue, &ptr, portMAX_DELAY)) {
                ISRData* valuePtr = static_cast<ISRData*>(ptr);
                // log_info("PCA state change ISR");
                valuePtr->updateValueFromDevice();
            }
        }
    }

    void PCA9539::init() {
        this->_i2cBus = config->_i2c;

        _isrQueue = xQueueCreate(16, sizeof(void*));
        xTaskCreatePinnedToCore(isrTaskLoop,                     // task
                                "isr_handler",                   // name for task
                                configMINIMAL_STACK_SIZE + 256,  // size of task stack
                                this,                            // parameters
                                1,                               // priority
                                &_isrHandler,
                                SUPPORT_TASK_CORE  // core
        );

        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            data._address   = uint8_t(0x74 + i);
            data._container = this;
            data._valueBase = reinterpret_cast<volatile uint16_t*>(&_value) + i;

            // Update the value first by reading it:
            data.updateValueFromDevice();

            if (!data._pin.undefined()) {
                data._pin.setAttr(Pin::Attr::ISR | Pin::Attr::Input);

                // The interrupt pin is 'active low'. So if it falls, we're interested in the new value.
                data._pin.attachInterrupt(updatePCAState, FALLING, &data);
            } else {
                // Reset valueBase so we know it's not bound to an ISR:
                data._valueBase = nullptr;
            }
        }
    }

    void PCA9539::ISRData::updateValueFromDevice() {
        const uint8_t InputReg = 0;
        auto          i2cBus   = _container->_i2cBus;

        auto     r1       = I2CGetValue(i2cBus, _address, InputReg);
        auto     r2       = I2CGetValue(i2cBus, _address, InputReg + 1);
        uint16_t oldValue = *_valueBase;
        uint16_t value    = (uint16_t(r2) << 8) | uint16_t(r1);
        *_valueBase       = value;

        if (_hasISR) {
            for (int i = 0; i < 16; ++i) {
                uint16_t mask = uint16_t(1) << i;

                if (_isrCallback[i] != nullptr && (oldValue & mask) != (value & mask)) {
                    // log_info("State change pin " << i);
                    switch (_isrMode[i]) {
                        case RISING:
                            if ((value & mask) == mask) {
                                _isrCallback[i](_isrArgument);
                            }
                            break;
                        case FALLING:
                            if ((value & mask) == 0) {
                                _isrCallback[i](_isrArgument);
                            }
                            break;
                        case CHANGE:
                            _isrCallback[i](_isrArgument);
                            break;
                    }
                }
            }
        }
    }

    void PCA9539::updatePCAState(void* ptr) {
        ISRData* valuePtr = static_cast<ISRData*>(ptr);

        BaseType_t xHigherPriorityTaskWoken = false;
        xQueueSendFromISR(valuePtr->_container->_isrQueue, &valuePtr, &xHigherPriorityTaskWoken);
    }

    void PCA9539::setupPin(pinnum_t index, Pins::PinAttributes attr) {
        bool activeLow = attr.has(Pins::PinAttributes::ActiveLow);
        bool output    = attr.has(Pins::PinAttributes::Output);

        uint64_t mask  = uint64_t(1) << index;
        _invert        = (_invert & ~mask) | (activeLow ? mask : 0);
        _configuration = (_configuration & ~mask) | (output ? 0 : mask);

        const uint8_t deviceId = index / 16;

        const uint8_t ConfigReg = 6;
        uint8_t       address   = 0x74 + deviceId;

        uint8_t value = uint8_t(_configuration >> (8 * (index / 8)));
        uint8_t reg   = ConfigReg + ((index / 8) & 1);

        // log_info("Setup reg " << int(reg) << " with value " << int(value));

        I2CSetValue(_i2cBus, address, reg, value);
    }

    void PCA9539::writePin(pinnum_t index, bool high) {
        uint64_t mask   = uint64_t(1) << index;
        uint64_t oldVal = _value;
        uint64_t newVal = high ? mask : uint64_t(0);
        _value          = (_value & ~mask) | newVal;

        _dirtyRegisters |= ((_value != oldVal) ? 1 : 0) << (index / 8);
    }

    bool PCA9539::readPin(pinnum_t index) {
        uint8_t reg      = uint8_t(index / 8);
        uint8_t deviceId = reg / 2;

        // If it's handled by the ISR, we don't need to read anything from the device.
        // Otherwise, we do. Check:
        if (_isrData[deviceId]._valueBase == nullptr) {
            const uint8_t InputReg = 0;
            uint8_t       address  = 0x74 + deviceId;

            auto     readReg  = InputReg + (reg & 1);
            auto     value    = I2CGetValue(_i2cBus, address, readReg);
            uint64_t newValue = uint64_t(value) << (int(reg) * 8);
            uint64_t mask     = uint64_t(0xff) << (int(reg) * 8);

            _value = ((newValue ^ _invert) & mask) | (_value & ~mask);

            // log_info("Read reg " << int(readReg) << " <- value " << int(newValue) << " gives " << int(_value));
        }
        // else {
        //     log_info("No read, value is " << int(_value));
        // }

        return (_value & (1ull << index)) != 0;
    }

    void PCA9539::flushWrites() {
        uint64_t write = _value ^ _invert;
        for (int i = 0; i < 8; ++i) {
            if ((_dirtyRegisters & (1 << i)) != 0) {
                const uint8_t OutputReg = 2;
                uint8_t       address   = 0x74 + (i / 2);

                uint8_t val = uint8_t(write >> (8 * i));
                uint8_t reg = OutputReg + (i & 1);
                I2CSetValue(_i2cBus, address, reg, val);
            }
        }

        _dirtyRegisters = 0;
    }

    // ISR's:
    void PCA9539::attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) {
        int device    = index / 16;
        int pinNumber = index % 16;

        Assert(_isrData[device]._isrCallback[pinNumber] == nullptr, "You can only set a single ISR for pin %d", index);

        _isrData[device]._isrCallback[pinNumber] = callback;
        _isrData[device]._isrArgument[pinNumber] = arg;
        _isrData[device]._isrMode[pinNumber]     = mode;
        _isrData[device]._hasISR                 = true;
    }

    void PCA9539::detachInterrupt(pinnum_t index) {
        int device    = index / 16;
        int pinNumber = index % 16;

        _isrData[device]._isrCallback[pinNumber] = nullptr;
        _isrData[device]._isrArgument[pinNumber] = nullptr;
        _isrData[device]._isrMode[pinNumber]     = 0;

        bool hasISR = false;
        for (int i = 0; i < 16; ++i) {
            hasISR |= (_isrData[device]._isrArgument[i] != nullptr);
        }
        _isrData[device]._hasISR = hasISR;
    }

    const char* PCA9539::name() const { return "pca9539"; }

    PCA9539 ::~PCA9539() {
        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            if (!data._pin.undefined()) {
                data._pin.detachInterrupt();
            }
        }
    }

    // Register extender:
    namespace {
        PinExtenderFactory::InstanceBuilder<PCA9539> registration("pca9539");
    }
}
