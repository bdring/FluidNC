// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicSpiDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC5160_RSENSE_DEFAULT = 0.075f;

/* 
Dump from an existing setup
[MSG:INFO: CHOPCONF: 0x13408158] 322994520
[MSG:INFO: COOLCONF: 0x0]
[MSG:INFO: THIGH: 0x0]
[MSG:INFO: TCOOLTHRS: 0x0]
[MSG:INFO: GCONF: 0x4] 4
[MSG:INFO: PWMCONF: 0xc40c001e] // 20468989982
[MSG:INFO: IHOLD_IRUN: 0x1f0c] // 7948

*/

namespace MotorDrivers {

    class TMC5160ProDriver : public TrinamicSpiDriver {
    public:
        TMC5160ProDriver(const char* name) : TrinamicSpiDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            //handler.item("tpfd", _tpfd, 0, 15);
            StandardStepper::group(handler);
            handler.item("cs_pin", _cs_pin);
            handler.item("spi_index", _spi_index, -1, 127);

            handler.item("use_enable", _use_enable);
            handler.item("CHOPCONF", CHOPCONF);
            handler.item("COOLCONF", COOLCONF);
            handler.item("THIGH", THIGH);
            handler.item("TCOOLTHRS", TCOOLTHRS);
            handler.item("GCONF", GCONF);
            handler.item("PWMCONF", PWMCONF);
            handler.item("IHOLD_IRUN", IHOLD_IRUN);
        }

    private:
        TMC5160Stepper* tmc5160 = nullptr;

        uint32_t CHOPCONF   = 322994520;
        uint32_t COOLCONF   = 0;
        uint32_t THIGH      = 0;
        uint32_t TCOOLTHRS  = 0;
        uint32_t GCONF      = 4;
        uint32_t PWMCONF    = 3289120798;
        uint32_t IHOLD_IRUN = 7948;

        bool test();
        void set_registers(bool isHoming);
    };
}
