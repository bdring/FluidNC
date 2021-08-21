#pragma once

/*
    TrinamicUartDriver.h

    Part of Grbl_ESP32
    2020 -	The Ant Team
    2020 -	Bart Dring

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../Uart.h"

#include <cstdint>

class TMC2209Stepper;  // Forward declaration

namespace MotorDrivers {

    class TrinamicUartDriver : public TrinamicBase {
    private:
        Uart* _uart = nullptr;

        static bool _uart_started;

        TMC2209Stepper* tmcstepper;  // all other driver types are subclasses of this one

        bool test();
        void set_mode(bool isHoming);
        void trinamic_test_response();
        void trinamic_stepper_enable(bool enable);

    protected:
        void config_message() override;

    public:
        TrinamicUartDriver(uint16_t driver_part_number) : TrinamicUartDriver(driver_part_number, get_next_index()) {}

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
