// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicUartDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC2208_RSENSE_DEFAULT = 0.11f;

namespace MotorDrivers {

    class TMC2208Driver : public TrinamicUartDriver {
    public:
        TMC2208Driver(const char* name) : TrinamicUartDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicUartDriver::group(handler);
            handler.item("run_mode", _run_mode, trinamicModes);
            handler.item("homing_mode", _homing_mode, trinamicModes);
            handler.item("stallguard", _stallguard, -64, 63);
            handler.item("stallguard_debug", _stallguardDebugMode);
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);
        }

    private:
        TMC2208Stepper* tmc2208 = nullptr;

        bool test();
        void set_registers(bool isHoming);
    };
}
