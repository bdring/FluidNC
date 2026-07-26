// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	The Ant Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TrinamicBase.h"
#include "Pin.h"
#include "Uart.h"

#include <cstdint>

namespace MotorDrivers {

    class TrinamicUartDriver : public TrinamicBase {
    public:
        TrinamicUartDriver(const char* name) : TrinamicBase(name) {}

        void init() override;

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
            // @config addr
            // @default 0
            // Hardware UART address of the chip. TMC2208/TMC2225 have a fixed address of 0
            // (this field has no effect on them); TMC2209/TMC2226 set their real address
            // via their MS1/MS2 pins, making them individually addressable (up to 4 chips
            // per UART bus).
            handler.item("addr", _addr);

            // @config cs_pin
            // @default NO_PIN
            // Rarely used -- present because this base class is shared with the SPI driver
            // family, but a UART-mode chip doesn't need a chip-select pin. Only relevant
            // for a cs_pin-based UART switching setup, and required to be NO_PIN if
            // shared_address_write_only is used (TMC2209 only, see TMC2209Driver).
            handler.item("cs_pin", _cs_pin);

            // @config uart_num
            // @default -1
            // Which top-level uartN: section this chip's UART register interface runs
            // over. Required -- afterParse() asserts this is actually set.
            handler.item("uart_num", _uart_num);

            TrinamicBase::group(handler);
        }

    protected:
        Uart* _uart = nullptr;

        Pin _cs_pin;

        int32_t _uart_num = -1;

        static bool _uart_started;
        void        config_message() override;

        uint8_t toffValue();  // TO DO move to Base?

    private:
    };

}
