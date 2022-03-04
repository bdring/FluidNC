// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2CExtender.h"

#include "Extenders.h"
#include "../Logging.h"
#include "../Assert.h"
#include "../Machine/I2CBus.h"

#include <esp_attr.h>
#include <esp32-hal-gpio.h>
#include <freertos/FreeRTOS.h>

namespace Extenders {
    EnumItem i2cDevice[] = { { int(I2CExtenderDevice::PCA9539), "pca9539" }, EnumItem(int(I2CExtenderDevice::Unknown)) };

    I2CExtender::I2CExtender() : _i2cBus(nullptr), _usedIORegisters(0), _dirtyWriteBuffer(0), _dirtyWrite(0), _status(0) {}

    uint8_t I2CExtender::I2CGetValue(uint8_t address, uint8_t reg) {
        auto bus = _i2cBus;

        int err;
        if ((err = bus->write(address, &reg, 1)) != 0) {
            log_warn("Cannot read from I2C bus: " << Machine::I2CBus::ErrorDescription(err));

            IOError();
            return 0;
        } else {
            uint8_t result = 0;
            if (bus->read(address, &result, 1) != 1) {
                log_warn("Cannot read from I2C bus: "
                         << "no response");

                IOError();
            } else {
                // This log line will probably generate a stack overflow and way too much data. Use with care:
                // log_info("Request address: " << int(address) << ", reg: " << int(reg) << " gives: " << int(result));
                errorCount = 0;
            }
            return result;
        }
    }

    void I2CExtender::I2CSetValue(uint8_t address, uint8_t reg, uint8_t value) {
        auto bus = _i2cBus;

        uint8_t data[2];
        data[0] = reg;
        data[1] = uint8_t(value);

        int err = bus->write(address, data, 2);

        if (err) {
            log_warn("Cannot write to I2C bus: " << Machine::I2CBus::ErrorDescription(err));
            IOError();
        } else {
            // This log line will probably generate a stack overflow and way too much data. Use with care:
            // log_info("Set address: " << int(address) << ", reg: " << int(reg) << " to: " << int(value));
            errorCount = 0;
        }
    }

    void I2CExtender::IOError() {
        if (errorCount != 0) {
            delay(errorCount * 10);
            if (errorCount < 50) {
                ++errorCount;
            }
        }

        // If an I/O error occurred, the best we can do is just reset the whole thing, and get it over with:
        _status |= 1;
        if (_outputReg != 0xFF) {
            _status |= 4;  // writes
        }
        if (_inputReg != 0xFF) {
            _status |= 8;  // reads
        }
    }

    void I2CExtender::isrTaskLoopDetail() {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
        int     registersPerDevice = _ports / 8;
        int     claimedValues      = 0;
        uint8_t commonStatus       = _operation;

        // Update everything the first operation
        IOError();

        // Main loop for I2C handling:
        while (true) {
            // If we set it to 0, we don't know if we can use the read data. 0x10 locks the status until we're done
            // reading
            uint8_t newStatus = 0x10;

            newStatus = _status.exchange(newStatus);
            newStatus |= commonStatus;

            if (newStatus != 0) {
                if ((newStatus & 2) != 0) {
                    _status = 0;
                    return;  // Stop running
                }

                // Update config:
                if ((newStatus & 1) != 0) {
                    // First fence!
                    std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);

                    // Configuration dirty. Update _configuration and _invert.
                    //
                    // First check how many u8's are claimed:
                    claimedValues = 0;
                    for (int i = 0; i < 8; ++i) {
                        if (_claimed.bytes[i] != 0) {
                            claimedValues = i + 1;
                        }
                    }
                    // Invert:
                    if (_invertReg != 0xFF) {
                        uint8_t currentRegister = _invertReg;
                        uint8_t address         = _address;

                        for (int i = 0; i < claimedValues; ++i) {
                            uint8_t by = _invert.bytes[i];
                            I2CSetValue(address, currentRegister, by);

                            currentRegister++;
                            if (currentRegister == registersPerDevice + _invertReg) {
                                ++address;
                            }
                        }
                    }
                    // Configuration:
                    {
                        uint8_t currentRegister = _operationReg;
                        uint8_t address         = _address;

                        for (int i = 0; i < claimedValues; ++i) {
                            uint8_t by = _configuration.bytes[i];
                            I2CSetValue(address, currentRegister, by);

                            currentRegister++;
                            if (currentRegister == registersPerDevice + _operationReg) {
                                ++address;
                            }
                        }
                    }

                    // Configuration changed. Writes and reads must be updated.
                    if (_outputReg != 0xFF) {
                        newStatus |= 4;      // writes
                        _dirtyWrite = 0xFF;  // everything is dirty.
                    }
                    if (_inputReg != 0xFF) {
                        newStatus |= 8;  // reads
                    }

                    commonStatus = _operation;
                }

                // Handle writes:
                if ((newStatus & 4) != 0) {
                    uint8_t currentRegister = _outputReg;
                    uint8_t address         = _address;

                    bool handleInvertSoftware = (_invertReg == 0xFF);

                    auto toWrite = _dirtyWrite.exchange(0);
                    for (int i = 0; i < claimedValues; ++i) {
                        if ((toWrite & (1 << i)) != 0) {
                            uint8_t by = handleInvertSoftware ? (_output.bytes[i] ^ _invert.bytes[i]) : _output.bytes[i];
                            I2CSetValue(address, currentRegister, by);
                        }

                        currentRegister++;
                        if (currentRegister == registersPerDevice + _outputReg) {
                            ++address;
                        }
                    }
                }

                // Handle reads:
                if ((newStatus & 8) != 0) {
                    uint8_t currentRegister = _inputReg;
                    uint8_t address         = _address;

                    // If we don't have an ISR, we must update everything. Otherwise, we can cherry pick:
                    bool handleInvertSoftware = (_invertReg == 0xFF);

                    uint8_t newBytes[8];
                    for (int i = 0; i < claimedValues; ++i) {
                        auto newByte = I2CGetValue(address, currentRegister);
                        if (handleInvertSoftware) {
                            newByte ^= _invert.bytes[i];
                        }
                        newBytes[i] = newByte;

                        currentRegister++;
                        if (currentRegister == registersPerDevice + _inputReg) {
                            ++address;
                        }
                    }

                    // Remove the busy flag, keep the rest. If we don't do that here, we
                    // end up with a race condition if we use _status in the ISR.
                    _status &= ~0x10;

                    for (int i = 0; i < claimedValues; ++i) {
                        auto oldByte = _input.bytes[i];
                        auto newByte = newBytes[i];

                        if (oldByte != newByte) {
                            // Handle ISR's:
                            _input.bytes[i] = newByte;
                            int offset      = i * 8;
                            for (int j = 0; j < 8; ++j) {
                                auto isr = _isrData[offset + j];
                                if (isr.defined()) {
                                    auto mask = uint8_t(1 << j);
                                    auto o    = (oldByte & mask);
                                    auto n    = (newByte & mask);
                                    if (o != n) {
                                        isr.callback(isr.data);  // bug; race condition
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Remove the busy flag, keep the rest.
            _status &= ~0x10;

            vTaskDelay(TaskDelayBetweenIterations);
        }
    }

    void I2CExtender::isrTaskLoop(void* arg) { static_cast<I2CExtender*>(arg)->isrTaskLoopDetail(); }

    void I2CExtender::claim(pinnum_t index) {
        Assert(index >= 0 && index < 64, "I2CExtender IO index should be [0-63]; %d is out of range", index);

        uint64_t mask = uint64_t(1) << index;
        Assert((_claimed.value & mask) == 0, "I2CExtender IO port %d is already used.", index);

        _claimed.value |= mask;
    }

    void I2CExtender::free(pinnum_t index) {
        uint64_t mask = uint64_t(1) << index;
        _claimed.value &= ~mask;
    }

    void I2CExtender::validate() const {
        auto i2c = config->_i2c;
        Assert(i2c != nullptr, "I2CExtender works through I2C, but I2C is not configured.");

        // We cannot validate _i2cBus, because that's initialized during `init`.
        Assert(_device != int(I2CExtenderDevice::Unknown), "I2C device type is unknown. Cannot continue initializing extender.");
    }

    void I2CExtender::group(Configuration::HandlerBase& handler) {
        //   device: pca9539
        //   device_id: 0
        //   interrupt: gpio.36
        handler.item("device", _device, i2cDevice);
        handler.item("device_id", _deviceId);
        handler.item("interrupt", _interruptPin);
    }

    void I2CExtender::interruptHandler(void* arg) {
        auto ext = static_cast<I2CExtender*>(arg);
        ext->_status |= 8;
    }

    void I2CExtender::init() {
        Assert(_isrHandler == nullptr, "Init has already been called on I2C extender.");
        this->_i2cBus = config->_i2c;

        switch (I2CExtenderDevice(_device)) {
            case I2CExtenderDevice::PCA9539:
                // See data sheet page 7+:
                _address      = 0x74 + _deviceId;
                _ports        = 16;
                _inputReg     = 0;
                _outputReg    = 2;
                _invertReg    = 4;
                _operationReg = 6;
                break;

            default:
                Assert(false, "Pin extender device is not supported!");
                break;
        }

        // Ensure data is available:
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);

        xTaskCreatePinnedToCore(isrTaskLoop,                            // task
                                "i2cHandler",                           // name for task
                                configMINIMAL_STACK_SIZE + 512 + 2048,  // size of task stack
                                this,                                   // parameters
                                1,                                      // priority
                                &_isrHandler,
                                SUPPORT_TASK_CORE  // core
        );

        if (_interruptPin.defined()) {
            _interruptPin.setAttr(Pin::Attr::ISR | Pin::Attr::Input);
            _interruptPin.attachInterrupt(interruptHandler, FALLING, this);
        }
    }

    void IRAM_ATTR I2CExtender::setupPin(pinnum_t index, Pins::PinAttributes attr) {
        Assert(index < 64 && index >= 0, "Pin index out of range");

        _usedIORegisters |= uint8_t(1 << (index / 8));

        uint64_t mask = 1ull << index;

        if (attr.has(Pins::PinAttributes::Input)) {
            _configuration.value |= mask;

            if (attr.has(Pins::PinAttributes::PullUp)) {
                _output.value |= mask;
            } else if (attr.has(Pins::PinAttributes::PullDown)) {
                _output.value &= ~mask;
            }
        } else if (attr.has(Pins::PinAttributes::Output)) {
            _configuration.value &= ~mask;

            if (attr.has(Pins::PinAttributes::InitialOn)) {
                _output.value |= mask;
            }
        }

        if (attr.has(Pins::PinAttributes::ActiveLow)) {
            _invert.value |= mask;
        }

        // Ignore the ISR flag. ISR is fine.

        // Trigger an update:
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
        _status |= 1;
    }

    void IRAM_ATTR I2CExtender::writePin(pinnum_t index, bool high) {
        Assert(index < 64 && index >= 0, "Pin index out of range");

        uint64_t mask     = 1ull << index;
        auto     oldValue = _output.value;
        if (high) {
            _output.value |= mask;
        } else {
            _output.value &= ~mask;
        }

        // Did something change?
        if (oldValue != _output.value) {
            uint8_t dirtyMask = uint8_t(1 << (index / 8));
            _dirtyWriteBuffer |= dirtyMask;
        }

        // Note that _status is *not* updated! flushWrites takes care of this!
    }

    bool IRAM_ATTR I2CExtender::readPin(pinnum_t index) {
        Assert(index < 64 && index >= 0, "Pin index out of range");

        // There are two possibilities here:
        // 1. We use an ISR, and that we can just use the information as-is as long as it's in sync.
        //    The ISR itself triggers the update.
        // 2. We don't use an ISR and need to update from I2C before we can reliably use the value.

        if (!_interruptPin.defined()) {
            _status |= 8;
        }
        while (_status != 0) {
            vTaskDelay(1);  // Must be <TaskDelayBetweenIterations and as small as possible.
        }

        // Use the value:
        return ((_input.value >> index) & 1) == 1;
    }

    void IRAM_ATTR I2CExtender::flushWrites() {
        auto writeMask = _dirtyWriteBuffer.exchange(0);

        _dirtyWrite |= writeMask;
        _status |= 4;

        while (_status != 0) {
            vTaskDelay(1);
        }
    }

    void I2CExtender::attachInterrupt(pinnum_t index, void (*callback)(void*), void* arg, int mode) {
        Assert(mode == CHANGE, "Only mode CHANGE is allowed for pin extender ISR's.");
        Assert(index < 64 && index >= 0, "Pin index out of range");

        // log_debug("Attaching interrupt (I2C) on index " << int(index));

        ISRData& data = _isrData[index];
        data.callback = callback;
        data.data     = arg;

        // Update continuous operation.
        _operation &= ~8;
        if (!_interruptPin.defined()) {
            _operation |= 8 | 16;
        }

        // Trigger task configuration update:
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // write fence first!
        _status |= 1;
    }

    void I2CExtender::detachInterrupt(pinnum_t index) {
        Assert(index < 64 && index >= 0, "Pin index out of range");

        // log_debug("Detaching interrupt (I2C) on index " << int(index));

        ISRData& data = _isrData[index];
        data.callback = nullptr;
        data.data     = nullptr;

        // Check if we still need to ISR everything. Use a temporary to ensure thread safety:
        auto newop = _operation;
        newop &= ~8;
        if (!_interruptPin.defined()) {
            for (int i = 0; i < 64; ++i) {
                if (_isrData[i].defined()) {
                    newop |= 8 | 16;
                }
            }
        }
        _operation = newop;

        // Trigger task configuration update:
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // write fence first!
        _status |= 1;
    }

    const char* I2CExtender::name() const { return "i2c_extender"; }

    I2CExtender::~I2CExtender() {
        // The task might have allocated temporary data, so we cannot just destroy it:
        _status |= 2;

        // Detach the interrupt pin:
        if (_interruptPin.defined()) {
            _interruptPin.detachInterrupt();
        }

        // Give enough time for the task to stop:
        for (int i = 0; i < 10 && _status != 0; ++i) {
            vTaskDelay(TaskDelayBetweenIterations);
        }

        // Should be safe now to stop.
        _isrHandler = nullptr;
    }

    // Register extender:
    namespace {
        PinExtenderFactory::InstanceBuilder<I2CExtender> registration("i2c_extender");
    }
}
