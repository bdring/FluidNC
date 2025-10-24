// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_I2C
#    include "Extenders.h"
#    include "I2CPinExtenderBase.h"
#    include "Logging.h"

// #    include <esp32-hal-gpio.h>
#    include <freertos/FreeRTOS.h>

namespace Extenders {
    void I2CPinExtenderBase::claim(pinnum_t index) {
        Assert(index >= 0 && index < 16 * 4, "I2C pin extender IO index should be [0-63]; %d is out of range", index);

        uint64_t mask = uint64_t(1) << index;
        Assert((_claimed & mask) == 0, "I2C pin extender IO port %d is already used", index);

        _claimed |= mask;
    }

    void I2CPinExtenderBase::free(pinnum_t index) {
        uint64_t mask = uint64_t(1) << index;
        _claimed &= ~mask;
    }

    uint8_t I2CPinExtenderBase::I2CGetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg) {
        auto err = bus->write(address, &reg, 1);

        if (err) {
            log_info("Error writing to i2c bus. Code: " << err);
            return 0;
        }

        uint8_t inputData;
        if (bus->read(address, &inputData, 1) != 1) {
            log_info("Error reading from i2c bus.");
        }

        return inputData;
    }

    void I2CPinExtenderBase::I2CSetValue(Machine::I2CBus* bus, uint8_t address, uint8_t reg, uint8_t value) {
        uint8_t data[2];
        data[0]  = reg;
        data[1]  = uint8_t(value);
        auto err = bus->write(address, data, 2);

        if (err) {
            log_error("Error writing to i2c bus; I2C pin extender failed. Code: " << err);
        }
    }

    void I2CPinExtenderBase::group(Configuration::HandlerBase& handler) {
        handler.item("busId", _i2cBusId);
        handler.item("interrupt0", _isrData[0]._pin);
        handler.item("interrupt1", _isrData[1]._pin);
        handler.item("interrupt2", _isrData[2]._pin);
        handler.item("interrupt3", _isrData[3]._pin);
    }

    void I2CPinExtenderBase::isrTaskLoop(void* arg) {
        auto inst = static_cast<I2CPinExtenderBase*>(arg);
        while (true) {
            void* ptr;
            if (xQueueReceive(inst->_isrQueue, &ptr, portMAX_DELAY)) {
                ISRData* valuePtr = static_cast<ISRData*>(ptr);
                // log_info("I2C pin extender state change ISR");
                valuePtr->updateValueFromDevice();
            }
        }
    }

    void I2CPinExtenderBase::init() {
        Assert(_i2cBusId >= 0 && _i2cBusId < 2, "I2C bus ID out of range");
#    if 0
        this->_i2cBus = config->_i2c[_i2cBusId];
#    endif

        auto i2c = _i2cBus;
        Assert(i2c != nullptr, "I2C pin extender only works when I2C bus is configured");

        log_info("Setting up I2C pin extender on I2C" << _i2cBusId);

        _isrQueue = xQueueCreate(16, sizeof(void*));
        xTaskCreatePinnedToCore(isrTaskLoop,                      // task
                                "isr_handler",                    // name for task
                                configMINIMAL_STACK_SIZE + 2048,  // size of task stack
                                this,                             // parameters
                                1,                                // priority
                                &_isrHandler,
                                SUPPORT_TASK_CORE  // core
        );

        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            data._address   = uint8_t(_baseAddress + i);
            data._container = this;
            data._valueBase = reinterpret_cast<volatile uint16_t*>(&_value) + i;

            if (!data._pin.undefined()) {
                // Update the value first by reading it:
                data.updateValueFromDevice();

                // Initialize ISR pin:
                data._pin.setAttr(Pin::Attr::ISR | Pin::Attr::Input);

#    if 0
                // The interrupt pin is 'active low'. So if it falls, we're interested in the new value.
                data._pin.attachInterrupt(updateRegisterState, FALLING, &data);
#    endif
            } else {
                // Reset valueBase so we know it's not bound to an ISR:
                data._valueBase = nullptr;
            }
        }
    }

    void I2CPinExtenderBase::ISRData::updateValueFromDevice() {
        const uint8_t InputReg = 0;
        auto          i2cBus   = _container->_i2cBus;

        auto     r1       = I2CGetValue(i2cBus, _address, InputReg);
        auto     r2       = I2CGetValue(i2cBus, _address, InputReg + 1);
        uint16_t oldValue = *_valueBase;
        uint16_t value    = (uint16_t(r2) << 8) | uint16_t(r1);
        *_valueBase       = value;

        // log_info("New I2C pin extender state: "; for (int i = 0; i < 16; ++i) { ss << (((value & (1 << i)) != 0) ? "x" : " "); });

        if (_hasISR) {
            for (int i = 0; i < 16; ++i) {
                uint16_t mask = uint16_t(1) << i;

                if (_isrCallback[i] != nullptr && (oldValue & mask) != (value & mask)) {
                    // log_info("State change pin " << i);
#    if 0
                    switch (_isrMode[i]) {
                        case RISING:
                            if ((value & mask) == mask) {
                                _isrCallback[i](_isrArgument[i], (value & mask) == mask);
                            }
                            break;
                        case FALLING:
                            if ((value & mask) == 0) {
                                _isrCallback[i](_isrArgument[i], (value & mask) == mask);
                            }
                            break;
                        case CHANGE:
                            _isrCallback[i](_isrArgument[i], (value & mask) == mask);
                            break;
                    }
#    endif
                }
            }
        }
    }

    void I2CPinExtenderBase::updateRegisterState(void* ptr, bool newState) {
        ISRData* valuePtr = static_cast<ISRData*>(ptr);

        BaseType_t xHigherPriorityTaskWoken = false;
        xQueueSendFromISR(valuePtr->_container->_isrQueue, &valuePtr, &xHigherPriorityTaskWoken);
    }

    void I2CPinExtenderBase::setupPin(pinnum_t index, Pins::PinAttributes attr) {
        bool activeLow = attr.has(Pins::PinAttributes::ActiveLow);
        bool output    = attr.has(Pins::PinAttributes::Output);

        uint64_t mask  = uint64_t(1) << index;
        _invert        = (_invert & ~mask) | (activeLow ? mask : 0);
        _configuration = (_configuration & ~mask) | (output ? 0 : mask);

        const uint8_t deviceId = index / 16;

        const uint8_t ConfigReg = 6;
        uint8_t       address   = _baseAddress + deviceId;

        uint8_t value = uint8_t(_configuration >> (8 * (index / 8)));
        uint8_t reg   = ConfigReg + ((index / 8) & 1);

        // log_info("Setup reg " << int(reg) << " with value " << int(value));

        I2CSetValue(_i2cBus, address, reg, value);
    }

    void I2CPinExtenderBase::writePin(pinnum_t index, bool high) {
        uint64_t mask   = uint64_t(1) << index;
        uint64_t oldVal = _value;
        uint64_t newVal = high ? mask : uint64_t(0);
        _value          = (_value & ~mask) | newVal;

        _dirtyRegisters |= ((_value != oldVal) ? 1 : 0) << (index / 8);
    }

    bool I2CPinExtenderBase::readPin(pinnum_t index) {
        uint8_t reg      = uint8_t(index / 8);
        uint8_t deviceId = reg / 2;

        // If it's handled by the ISR, we don't need to read anything from the device.
        // Otherwise, we do. Check:
        if (_isrData[deviceId]._valueBase == nullptr) {
            const uint8_t InputReg = 0;
            uint8_t       address  = _baseAddress + deviceId;

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

    void I2CPinExtenderBase::flushWrites() {
        uint64_t write = _value ^ _invert;
        for (int i = 0; i < 8; ++i) {
            if ((_dirtyRegisters & (1 << i)) != 0) {
                const uint8_t OutputReg = 2;
                uint8_t       address   = _baseAddress + (i / 2);

                uint8_t val = uint8_t(write >> (8 * i));
                uint8_t reg = OutputReg + (i & 1);
                I2CSetValue(_i2cBus, address, reg, val);
            }
        }

        _dirtyRegisters = 0;
    }

    // ISR's:
    void I2CPinExtenderBase::attachInterrupt(pinnum_t index, void (*callback)(void*, bool), void* arg, uint8_t mode) {
        uint8_t  device    = index / 16;
        pinnum_t pinNumber = index % 16;

        Assert(_isrData[device]._isrCallback[pinNumber] == nullptr, "You can only set a single ISR for pin %d", index);

        _isrData[device]._isrCallback[pinNumber] = callback;
        _isrData[device]._isrArgument[pinNumber] = arg;
        _isrData[device]._isrMode[pinNumber]     = mode;
        _isrData[device]._hasISR                 = true;
    }

    void I2CPinExtenderBase::detachInterrupt(pinnum_t index) {
        uint8_t  device    = index / 16;
        pinnum_t pinNumber = index % 16;

        _isrData[device]._isrCallback[pinNumber] = nullptr;
        _isrData[device]._isrArgument[pinNumber] = nullptr;
        _isrData[device]._isrMode[pinNumber]     = 0;

        bool hasISR = false;
        for (int i = 0; i < 16; ++i) {
            hasISR |= (_isrData[device]._isrArgument[i] != nullptr);
        }
        _isrData[device]._hasISR = hasISR;
    }

    I2CPinExtenderBase::~I2CPinExtenderBase() {
        for (int i = 0; i < 4; ++i) {
            auto& data = _isrData[i];

            if (!data._pin.undefined()) {
#    if 0
                data._pin.detachInterrupt();
#    endif
            }
        }
    }
}
#endif
