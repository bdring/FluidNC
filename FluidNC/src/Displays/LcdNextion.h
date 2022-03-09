// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	
*/

#include "Display.h"
#include "../Uart.h"

namespace Displays {
    class LcdNextion : public Display {
    protected:
        int          _geometry = 0;
        bool         _flip     = false;
        static Uart* _uart;
        static bool  _uart_started;

        static void sendDROs();
        static void timed_update(void* pvParameters);

    public:
        void init() override;

        void config_message() override;

        void update(statusCounter sysCounter) override;

        void validate() const override { Assert(_uart != nullptr, "lcs_nextion: Missing UART configuration"); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("geometry", _geometry);
            handler.item("flip", _flip);

            // if (_uart == nullptr) {
            //     // If _uart is null this must be the parsing phase
            //     handler.section("uart", _uart);
            //     // If we just defined _uart, record that this is the enclosing instance
            //     if (_uart != nullptr) {
            //         _my_uart = true;
            //     }
            // } else if (_my_uart) {
            //     // _uart is already defined and this is the enclosing instance, so we
            //     // handle the uart section in a non-parsing phase
            //     handler.section("uart", _uart);
            // }

            handler.section("uart", _uart);
        }

        virtual ~LcdNextion() {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "lcd_nextion"; }

    protected:
    };
}
