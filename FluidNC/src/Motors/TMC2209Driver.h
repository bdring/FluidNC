// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicUartDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC2209_RSENSE_DEFAULT = 0.11f;

namespace MotorDrivers {

    class TMC2209Driver : public TrinamicUartDriver {
    public:
        TMC2209Driver(const char* name) : TrinamicUartDriver(name) {}

        // Overrides for inherited methods
        void init() override;
        void set_disable(bool disable);
        void config_motor() override;
        void debug_message() override;
        void validate() override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            TrinamicUartDriver::group(handler);

            handler.item("addr", _addr);
            handler.item("run_mode", _run_mode, trinamicModes);
            handler.item("homing_mode", _homing_mode, trinamicModes);
            handler.item("homing_amps", _homing_current, 0.0, 10.0);
            handler.item("stallguard", _stallguard, 0, 255);
            handler.item("stallguard_debug", _stallguardDebugMode);
            handler.item("toff_coolstep", _toff_coolstep, 2, 15);
        }

        void afterParse() override {
            TrinamicUartDriver::afterParse();
            if (_homing_current == 0) {
                _homing_current = _run_current;
            }
        }

    private:
        TMC2209Stepper* tmc2209 = nullptr;

        bool test();
        void set_registers(bool isHoming);
    };
}
