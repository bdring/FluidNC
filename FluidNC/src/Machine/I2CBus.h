// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"

#include <esp_attr.h>

class TwoWire;

namespace Machine {
    class I2CBus : public Configuration::Configurable {
    private:
        bool _error = false;

    public:
        I2CBus(int busNumber);

        int      _busNumber = 0;
        Pin      _sda;
        Pin      _scl;
        uint32_t _frequency = 100000;

        void init();
        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        int write(uint8_t address, const uint8_t* data, size_t count);
        int read(uint8_t address, uint8_t* data, size_t count);

        ~I2CBus() = default;
    };
}
