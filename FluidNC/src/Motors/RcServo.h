// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "PwmServo.h"
#include "System.h"

namespace MotorDrivers {
    class RcServo : public PwmServo {
    protected:
        int32_t _timer_ms = 20;

        void config_message() override;

        void set_location();

        uint32_t _pwm_freq = 50;  // 50 Hz is standard for analog servos. Digital ones can repeat faster

        uint32_t _min_pulse_us = 1000;  // microseconds
        uint32_t _max_pulse_us = 2000;  // microseconds

        uint32_t _min_pulse_cnt = 0;  // microseconds
        uint32_t _max_pulse_cnt = 0;  // microseconds

        steps_t _min_steps;
        steps_t _max_steps;

        bool _has_errors = false;

    public:
        RcServo(const char* name) : PwmServo(name) {}
        ~RcServo() {}

        void read_settings();

        // Overrides for inherited methods
        void init() override;
        bool set_homing_mode(bool isHoming) override;
        void set_disable(bool disable) override;
        void update() override;

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {
            // A hobby RC servo used as a virtual linear/rotary axis. The servo's physical
            // rotation range maps onto the enclosing axis's max_travel_mm; to reverse
            // direction, swap min_pulse_us/max_pulse_us rather than inverting the pin.
            // soft_limits: true is strongly recommended on any axis using this driver.

            // @config output_pin
            // @default NO_PIN
            // @pin_attributes pwm
            // PWM signal output to the servo.
            handler.item("output_pin", _output_pin);

            // @config pwm_hz
            // @default 50
            // @tuning typical
            // Servo PWM pulse repetition rate. 50Hz is the standard analog-servo value;
            // some digital servos can repeat faster.
            handler.item("pwm_hz", _pwm_freq, 50, 200);

            // @config min_pulse_us
            // @default 1000
            // @tuning per-machine
            // Pulse width, in microseconds, corresponding to one end of the servo's travel.
            handler.item("min_pulse_us", _min_pulse_us, 500, 2500);

            // @config max_pulse_us
            // @default 2000
            // @tuning per-machine
            // Pulse width, in microseconds, corresponding to the other end of the servo's
            // travel.
            handler.item("max_pulse_us", _max_pulse_us, 500, 2500);

            // @config timer_ms
            // @default 20
            // @tuning typical
            // Update interval, in milliseconds, for refreshing the servo's PWM position.
            handler.item("timer_ms", _timer_ms, 20, 250);

            Servo::group(handler);
        }
    };
}
