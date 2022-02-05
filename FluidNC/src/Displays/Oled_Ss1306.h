// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	
*/

#include "Display.h"

namespace Displays {
    class Oled_Ss1306 : public Display {
    protected:
        Pin      _sda_pin;
        Pin      _scl_pin;
        uint32_t _addr     = 0x3c;
        int      _geometry = 0;
        bool     _flip     = false;

    public:
        void init() override;

        void config_message() override;

        static void update(void* pvParameters);
        static void radioInfo();
        static void DRO();
        static void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked);

        void group(Configuration::HandlerBase& handler) override {
            handler.item("sda_pin", _sda_pin);
            handler.item("scl_pin", _scl_pin);
            handler.item("address", _addr);
            handler.item("geometry", _geometry);
            handler.item("flip", _flip);
        }

        virtual ~Oled_Ss1306() {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "oled_ss1306"; }

    protected:
    };
}
