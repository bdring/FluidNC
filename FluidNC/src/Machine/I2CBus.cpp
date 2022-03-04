// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2CBus.h"

#include <Wire.h>
#include <esp32-hal-i2c.h>

namespace Machine {
    void I2CBus::validate() const {
        if (_sda.defined() || _scl.defined()) {
            Assert(_sda.defined(), "I2C SDA pin should be configured once");
            Assert(_scl.defined(), "I2C SCL pin should be configured once");
            Assert(_busNumber == 0 || _busNumber == 1, "The ESP32 only has 2 I2C buses. Number %d is invalid", _busNumber);
        }
    }

    void I2CBus::group(Configuration::HandlerBase& handler) {
        handler.item("sda", _sda);
        handler.item("scl", _scl);
        handler.item("bus", _busNumber);
        handler.item("frequency", _frequency);
    }

    void I2CBus::init() {
        auto sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        auto sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        Assert(_busNumber == 0 || _busNumber == 1, "Bus # has to be 0 or 1; the ESP32 does not have more i2c peripherals.");

        if (_busNumber == 0) {
            i2c = &Wire;
        } else {
            i2c = &Wire1;
        }
        i2c->begin(int(sdaPin), int(sclPin), _frequency);

        log_info("I2C SDA: " << _sda.name() << ", SCL: " << _scl.name() << ", Freq: " << _frequency << ", Bus #: " << _busNumber);
    }

    const char* I2CBus::ErrorDescription(int code) {
        switch (code) {
            case I2C_ERROR_OK:
                return "ok";
            case I2C_ERROR_DEV:
                return "general device error";
            case I2C_ERROR_ACK:
                return "no ack returned by device";
            case I2C_ERROR_TIMEOUT:
                return "timeout";
            case I2C_ERROR_BUS:
                return "bus error";
            case I2C_ERROR_BUSY:
                return "device busy";
            case I2C_ERROR_MEMORY:
                return "insufficient memory";
            case I2C_ERROR_CONTINUE:
                return "continue";
            case I2C_ERROR_NO_BEGIN:
                return "begin transmission missing";
            default:
                return "unknown";
        }
    }
    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        // log_debug("I2C write addr=" << int(address) << ", count=" << int(count) << ", data " << (data ? "non null" : "null") << ", i2c "
        //                             << (i2c ? "non null" : "null"));

        i2c->beginTransmission(address);
        for (size_t i = 0; i < count; ++i) {
            i2c->write(data[i]);
        }
        return i2c->endTransmission();  // i2c_err_t, see header file
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        // log_debug("I2C read addr=" << int(address) << ", count=" << int(count) << ", data " << (data ? "non null" : "null") << ", i2c "
        //                            << (i2c ? "non null" : "null"));

        size_t c = i2c->requestFrom((int)address, count);

        for (size_t i = 0; i < c; ++i) {
            data[i] = i2c->read();
        }
        return c;
    }

}
