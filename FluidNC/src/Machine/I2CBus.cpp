// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2CBus.h"

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
        handler.item("busNumber", _busNumber);
        handler.item("frequency", _frequency);
    }

    void I2CBus::init() {
        log_info("I2C SDA:" << _sda.name() << ", SCL:" << _scl.name() << ", Bus:" << _busNumber);

        auto sdaPin = _sda.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);
        auto sclPin = _scl.getNative(Pin::Capabilities::Native | Pin::Capabilities::Input | Pin::Capabilities::Output);

        if (_busNumber == 0) {
            i2c = &Wire;
        } else {
            i2c = &Wire1;
        }
        i2c->begin(sdaPin, sclPin /*, _frequency */);
    }

    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        i2c->beginTransmission(address);
        for (size_t i = 0; i < count; ++i) {
            i2c->write(data[i]);
        }
        return i2c->endTransmission();  // i2c_err_t ??
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (i2c->requestFrom((int)address, 1) != 1) {
                return i;
            }
            data[i] = i2c->read();
        }
        return count;
    }

}
