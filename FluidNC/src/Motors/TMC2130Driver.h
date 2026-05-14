// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicSpiDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC2130_RSENSE_DEFAULT = 0.11f;

namespace MotorDrivers {

    class TMC2130Driver : public TrinamicSpiDriver {
    public:
        TMC2130Driver(const char* name) : TrinamicSpiDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

    private:
        TMC2130Stepper* tmc2130 = nullptr;

        bool test();
        void set_registers(bool isHoming) override;
    };
}
