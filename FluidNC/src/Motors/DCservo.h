// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    For control of Dynamixel Servos using Protocol 2.0
*/

#include "Servo.h"
#include "../Pin.h"

#include "../Uart.h"

#include <cstdint>

#include "../Maslow/Maslow.h"

namespace MotorDrivers {
    class DCservo : public Servo {
    protected:
        void config_message() override;

        void set_location();

        uint8_t _id;

        static int _timer_ms;

        static uint8_t _tx_message[100];  // outgoing to dynamixel
        static uint8_t _msg_index;
        static uint8_t _rx_message[50];  // received from dynamixel

        static TimerHandle_t _timer;

        static std::vector<DCservo*> _instances;

        int _axis_index;

        bool        _disabled;
        static bool _has_errors;

    public:
        DCservo() : _id(255), _disabled(true) {}

        // Overrides for inherited methods
        void        init() override;
        void        read_settings() override;
        bool        set_homing_mode(bool isHoming) override;
        void        set_disable(bool disable) override;
        void        update() override;
        static void update_all();
        void        config_motor() override;

        const char* name() override { return "dc_servo"; }

        // Configuration handlers:
        void validate() override {
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("id", _id);
            handler.item("timer_ms", _timer_ms);

            Servo::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "dc_servo"; }
    };
}
