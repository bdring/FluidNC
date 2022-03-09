// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	
*/

#include "Display.h"
#include "../Uart.h"

namespace Displays {
    class UartDisplay : public Display {
    protected:       
        static Uart* _uart;
        static bool  _uart_started;

        static void timed_update(void* pvParameters);

        int32_t _lastDROUpdate;


    public:
        void init() override;

        void config_message() override;

        void update(statusCounter sysCounter) override;

        void validate() const override { Assert(_uart != nullptr, "lcs_nextion: Missing UART configuration"); }

        void group(Configuration::HandlerBase& handler) override {
            handler.section("uart", _uart);
        }

        virtual ~UartDisplay() {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "uart_display"; }

    };
}
