// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"

#include <esp_attr.h>

class TwoWire;

namespace Machine {
    class I2CBus : public Configuration::Configurable {
    protected:
        TwoWire* i2c = nullptr;

    public:
        I2CBus() = default;

        int      _busNumber = 0;
        Pin      _sda;
        Pin      _scl;
        uint32_t _frequency = 0;

        void init();
        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        /* 
        typedef enum {
            I2C_ERROR_OK=0,
            I2C_ERROR_DEV,
            I2C_ERROR_ACK,
            I2C_ERROR_TIMEOUT,
            I2C_ERROR_BUS,
            I2C_ERROR_BUSY,
            I2C_ERROR_MEMORY,
            I2C_ERROR_CONTINUE,
            I2C_ERROR_NO_BEGIN
        } i2c_err_t;
        */

        int IRAM_ATTR write(uint8_t address, const uint8_t* data, size_t count);  // return i2c_err_t ?? Or is it mapped? TODO FIXME!
        int IRAM_ATTR read(uint8_t address, uint8_t* data, size_t count);

        ~I2CBus() = default;
    };
}
