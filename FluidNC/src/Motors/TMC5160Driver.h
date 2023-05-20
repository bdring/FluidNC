// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicSpiDriver.h"
#include "../Pin.h"
#include "../PinMapper.h"

#include <cstdint>

const float TMC5160_RSENSE_DEFAULT = 0.075f;

namespace MotorDrivers {

    class TMC5160Driver : public TrinamicSpiDriver {
    public:
        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicSpiDriver::group(handler);
            handler.item("tpfd", _tpfd, 0, 15);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_5160"; }

    private:
        TMC5160Stepper* tmc5160 = nullptr;

        uint8_t _tpfd = 4;

        bool test();
        void set_registers(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);
    };
}
