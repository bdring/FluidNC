// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicSpiDriver.h"
#include "../Pin.h"
#include "../PinMapper.h"

#include <cstdint>

const float TMC5160_RSENSE_DEFAULT = 0.075f;

namespace MotorDrivers {

    class TMC5160ProDriver : public TrinamicSpiDriver {
    public:
        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() const override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicSpiDriver::group(handler);
            //handler.item("tpfd", _tpfd, 0, 15);
            handler.item("CHOPCONF", CHOPCONF);
            handler.item("COOLCONF", COOLCONF);
            handler.item("THIGH", THIGH);
            handler.item("TCOOLTHRS", TCOOLTHRS);
            handler.item("GCONF", GCONF);
            handler.item("PWMCONF", PWMCONF);
            handler.item("IHOLD_IRUN", IHOLD_IRUN);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_5160Pro"; }

    private:
        TMC5160Stepper* tmc5160 = nullptr;

        uint32_t CHOPCONF = 0;
        uint32_t COOLCONF = 0;
        uint32_t THIGH    = 0;
        uint32_t TCOOLTHRS = 0;
        uint32_t GCONF     = 0;
        uint32_t PWMCONF   = 0;
        uint32_t IHOLD_IRUN = 0;

        bool test();
        void set_registers(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);
    };
}
