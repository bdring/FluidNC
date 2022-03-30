// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

//#include "TrinamicBase.h"
#include "TrinamicUartDriver.h"
#include "../Pin.h"
#include "../PinMapper.h"

#include <cstdint>

namespace MotorDrivers {

    class TMC2209Driver : public TrinamicUartDriver {
    public:
        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() const override { StandardStepper::validate(); }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_2209"; }

    private:
        TMC2209Stepper* tmc2209 = nullptr;

        bool test();
        void set_registers(bool isHoming);
        //void set_homing_mode();
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);
    };
}
