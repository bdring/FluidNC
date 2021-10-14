// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	The Ant Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../Uart.h"

#include <cstdint>

class TMC2208Stepper;  // Forward declaration
class TMC2209Stepper;  // Forward declaration

namespace MotorDrivers {

    class TrinamicUartDriver : public TrinamicBase {
    private:
        static Uart* _uart;

        static bool _uart_started;

        TMC2208Stepper* tmc2208 = nullptr;
        TMC2209Stepper* tmc2209 = nullptr;

        bool test();
        void set_mode(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);

    protected:
        void config_message() override;

    public:
        TrinamicUartDriver(uint16_t driver_part_number) : TrinamicUartDriver(driver_part_number, -1) {}

        TrinamicUartDriver(uint16_t driver_part_number, uint8_t address);

        void init() override;
        void read_settings() override;
        bool set_homing_mode(bool is_homing) override;
        void set_disable(bool disable) override;

        void debug_message();

        bool hw_serial_init();

        uint8_t _addr;

        // Configuration handlers:
        void validate() const override { StandardStepper::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.section("uart", _uart);
            handler.item("addr", _addr);
            TrinamicBase::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "trinamic_uart"; }
    };

    class TMC2208 : public TrinamicUartDriver {
    public:
        TMC2208() : TrinamicUartDriver(2208) {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_2208"; }
    };

    class TMC2209 : public TrinamicUartDriver {
    public:
        TMC2209() : TrinamicUartDriver(2209) {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "tmc_2209"; }
    };
}
