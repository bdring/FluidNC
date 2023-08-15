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

        static Uart* _uart;

        int _uart_num = -1;

        static bool _uart_started;

        static const int DXL_RESPONSE_WAIT_TICKS = 20;  // how long to wait for a response

        // protocol 2 byte positions
        static const int DXL_MSG_HDR1  = 0;
        static const int DXL_MSG_HDR2  = 1;
        static const int DXL_MSG_HDR3  = 2;
        static const int DXL_MSG_RSRV  = 3;  // reserved byte
        static const int DXL_MSG_ID    = 4;
        static const int DXL_MSG_LEN_L = 5;
        static const int DXL_MSG_LEN_H = 6;
        static const int DXL_MSG_INSTR = 7;
        static const int DXL_MSG_START = 8;

        static const int DXL_BROADCAST_ID = 0xFE;

        // protocol 2 instruction numbers
        static const int  DXL_INSTR_PING = 0x01;
        static const char DXL_REBOOT     = char(0x08);
        static const int  PING_RSP_LEN   = 14;
        static const char DXL_READ       = char(0x02);
        static const char DXL_WRITE      = char(0x03);
        static const char DXL_SYNC_WRITE = char(0x83);

        // protocol 2 register locations
        static const int DXL_OPERATING_MODE   = 11;
        static const int DXL_ADDR_TORQUE_EN   = 64;
        static const int DXL_ADDR_LED_ON      = 65;
        static const int DXL_GOAL_POSITION    = 116;  // 0x74
        static const int DXL_PRESENT_POSITION = 132;  // 0x84

        // control modes
        static const int DXL_CONTROL_MODE_POSITION = 3;

        uint32_t _countMin = 1024;
        uint32_t _countMax = 3072;

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
            Assert(_uart_num != -1, "Dynamixel: Missing uart_num configuration");
            Assert(_id != 255, "Dynamixel: ID must be configured.");
        }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("uart_num", _uart_num);
            handler.item("id", _id);
            handler.item("count_min", _countMin);
            handler.item("count_max", _countMax);
            handler.item("timer_ms", _timer_ms);

            Servo::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "dc_servo"; }
    };
}
