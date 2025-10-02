// Copyright (c) 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_I2C
#    include "I2CBus.h"
#    include "Driver/fluidnc_i2c.h"

namespace Machine {
    I2CBus::I2CBus(uint32_t busNumber) : _busNumber(busNumber) {}

    void I2CBus::validate() {
        if (_sda.defined() || _scl.defined()) {
            Assert(_sda.defined(), "I2C SDA pin configured multiple times");
            Assert(_scl.defined(), "I2C SCL pin configured multiple times");
        }
    }

    void I2CBus::group(Configuration::HandlerBase& handler) {
        handler.item("sda_pin", _sda);
        handler.item("scl_pin", _scl);
        handler.item("frequency", _frequency);
    }

    void I2CBus::init() {
        _error      = false;
        auto sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        auto sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        log_info("I2C SDA: " << _sda.name() << ", SCL: " << _scl.name() << ", Freq: " << _frequency << ", Bus #: " << _busNumber);

        _error = i2c_master_init(_busNumber, sdaPin, sclPin, _frequency);
        if (_error) {
            log_error("I2C init failed");
        }

        uint8_t nDevices = 0;
        log_info("Scanning...");
        for (uint8_t address = 1; address < 127; address++) {
            uint8_t buf[1];
            auto    error = i2c_write(_busNumber, address, buf, 0);
            if (error == 0) {
                log_info("I2C device found at address " << int(address));
                nDevices++;
            }
        }
        if (nDevices == 0) {
            log_info("No I2C devices found");
        } else {
            log_info("done");
        }
    }

    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }

        log_info("I2C write: " << int(address); for (int i = 0; i < count; ++i) { ss << ' ' << int(data[i]); });

        return i2c_write(_busNumber, address, data, count);
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }
        log_info("I2C read: " << int(address));
        return i2c_read(_busNumber, address, data, count);
    }
#    if 0
    I2CBus::I2CBus(uint8_t busNumber) : _busNumber(busNumber) {}

    void I2CBus::validate() {
        if (_sda.defined() || _scl.defined()) {
            Assert(_sda.defined(), "I2C SDA pin should be configured once");
            Assert(_scl.defined(), "I2C SCL pin should be configured once");
            Assert(_busNumber == 0 || _busNumber == 1, "The ESP32 only has 2 I2C buses. Number %d is invalid", _busNumber);
        }
    }

    void I2CBus::group(Configuration::HandlerBase& handler) {
        handler.item("sda_pin", _sda);
        handler.item("scl_pin", _scl);
        handler.item("frequency", _frequency);
    }

    void I2CBus::init() {
        log_info("I2C SDA: " << _sda.name() << ", SCL: " << _scl.name() << ", Freq: " << _frequency << ", Bus #: " << _busNumber);

        auto sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        auto sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        Assert(_busNumber == 0 || _busNumber == 1, "Bus # has to be 0 or 1; the ESP32 does not have more i2c peripherals.");

        if (_busNumber == 0) {
            i2c = &Wire;
        } else {
            i2c = &Wire1;
        }
        i2c->begin(int(sdaPin), int(sclPin), _frequency);

        uint8_t nDevices = 0;
        log_info("Scanning...");
        for (uint8_t address = 1; address < 127; address++) {
            i2c->beginTransmission(address);
            auto error = i2c->endTransmission();
            if (error == 0) {
                log_info("I2C device found at address " << int(address));
                nDevices++;
            }
        }
        if (nDevices == 0) {
            log_info("No I2C devices found");
        } else {
            log_info("done");
        }
    }

    const char* I2CBus::ErrorDescription(int code) {
        return esp_err_to_name(code);
    }

    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        // log_info("I2C write addr=" << int(address) << ", count=" << int(count) << ", data " << (data ? "non null" : "null") << ", i2c "
        //                             << (i2c ? "non null" : "null"));

        i2c->beginTransmission(address);
        for (size_t i = 0; i < count; ++i) {
            i2c->write(data[i]);
        }
        return i2c->endTransmission();  // i2c_err_t, see header file
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        // log_info("I2C read addr=" << int(address) << ", count=" << int(count) << ", data " << (data ? "non null" : "null") << ", i2c "
        //                            << (i2c ? "non null" : "null"));

        size_t c = i2c->requestFrom((int)address, count);

        for (size_t i = 0; i < c; ++i) {
            data[i] = i2c->read();
        }
        return c;
    }
#    endif
}
#endif
