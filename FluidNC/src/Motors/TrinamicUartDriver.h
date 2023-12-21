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

        uint8_t _addr;

        // Configuration handlers:
        void validate() override { StandardStepper::validate(); }

        void afterParse() override {
            StandardStepper::validate();
            Assert(_uart_num != -1, "TrinamicUartDriver must set uart_num: ");
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("addr", _addr);
            handler.item("cs_pin", _cs_pin);
            handler.item("uart_num", _uart_num);

            TrinamicBase::group(handler);
        }

    protected:
        Uart* _uart = nullptr;

        Pin _cs_pin;

        int _uart_num = -1;

        static bool _uart_started;
        void        config_message() override;

        uint8_t toffValue();  // TO DO move to Base?

    private:

    };

}
