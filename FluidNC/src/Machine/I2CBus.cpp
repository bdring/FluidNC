// Copyright (c) 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_I2C
#    include "I2CBus.h"
#    include "Driver/fluidnc_i2c.h"

namespace Machine {
    I2CBus::I2CBus(objnum_t busNumber) : _busNumber(busNumber) {}

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

#    if SCAN_I2C_BUS
        // This should be a command instead of something that happens automatically on init
        objnum_t nDevices = 0;
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
#    endif
    }

    int I2CBus::write(uint8_t address, const uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }

        log_verbose("I2C write: " << int(address); for (int i = 0; i < count; ++i) { ss << ' ' << int(data[i]); });

        auto ret = i2c_write(_busNumber, address, data, count);
        if (ret != count) {
            log_warn("Error writing to I2C device " << ret);
        }
        return ret;
    }

    int I2CBus::read(uint8_t address, uint8_t* data, size_t count) {
        if (_error) {
            return -1;
        }
        log_verbose("I2C read: " << int(address));
        return i2c_read(_busNumber, address, data, count);
    }
}
#endif
