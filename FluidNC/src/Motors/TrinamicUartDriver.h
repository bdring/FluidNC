// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	The Ant Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "../Pin.h"
#include "../Uart.h"

#include <cstdint>

namespace MotorDrivers {

    class TrinamicUartDriver : public TrinamicBase {
    public:
        TrinamicUartDriver() = default;

        void init() override;
        //void read_settings() override;
        //bool set_homing_mode(bool is_homing) override;
        void set_disable(bool disable) override;

        void debug_message();

        bool hw_serial_init();

        // TMC2208 and TMC2225 have a fixed addr = 0
        // TMC2209 and TMC2226 configure these through MS1/MS2.
        uint8_t _addr = 0;

        // Configuration handlers:
        void validate() override { StandardStepper::validate(); }

        void afterParse() override {
            StandardStepper::validate();
            Assert(_uart_num != -1, "TrinamicUartDriver must set uart_num: ");
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("uart_num", _uart_num);
            TrinamicBase::group(handler);
        }

    protected:
        Uart* _uart = nullptr;

        int _uart_num = -1;

        static bool _uart_started;
        void        config_message() override;

        uint8_t toffValue();  // TO DO move to Base?

    private:
        bool test();
        void set_mode(bool isHoming);
    };

}
