// Copyright (c) 2021 - Stefan de Bruijn
// Copyright (c) 2021 - Mitch Bradley
// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "Pin.h"

#include <cstdint>

namespace Machine {
    class ParlioBus : public Configuration::Configurable {
    public:
        static constexpr int NUM_DATA_PINS = 16;

        Pin _data[NUM_DATA_PINS];
        Pin _clk;

        int8_t _data_native[NUM_DATA_PINS] = {};
        bool   _data_defined[NUM_DATA_PINS] = {};
        int8_t _clk_native = -1;

        ParlioBus() = default;

        void validate() override;
        void group(Configuration::HandlerBase& handler) override;
        void init();

        ~ParlioBus() = default;
    };
}
