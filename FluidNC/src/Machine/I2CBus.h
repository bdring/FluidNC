// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"

namespace Machine {
    class I2CBus : public Configuration::Configurable {
    protected:
        bool _error = false;

    public:
        I2CBus(int32_t busNumber);

        int32_t  _busNumber = 0;
        Pin      _sda;
        Pin      _scl;
        uint32_t _frequency = 100000;

        void init();
        void validate() override;
        void group(Configuration::HandlerBase& handler) override;

        static const char* ErrorDescription(int32_t code);

        int32_t write(uint8_t address, const uint8_t* data, size_t count);
        int32_t read(uint8_t address, uint8_t* data, size_t count);

        ~I2CBus() = default;
    };
}
