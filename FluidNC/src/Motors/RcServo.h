// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Servo.h"
#include "RcServoSettings.h"

namespace MotorDrivers {
    class RcServo : public Servo {
    protected:
        int _timer_ms = 20;

        void config_message() override;

        void set_location();

        Pin      _output_pin;
        uint32_t _pwm_freq = SERVO_PWM_FREQ_DEFAULT;  // 50 Hz
        uint32_t _current_pwm_duty;

        bool _disabled;

        uint32_t _min_pulse_us = SERVO_PULSE_US_MIN_DEFAULT;  // microseconds
        uint32_t _max_pulse_us = SERVO_PULSE_US_MAX_DEFAULT;  // microseconds

        uint32_t _min_pulse_cnt = 0;  // microseconds
        uint32_t _max_pulse_cnt = 0;  // microseconds

        int _axis_index = -1;

        bool _has_errors = false;

    public:
        RcServo(const char* name) : Servo(name) {}
        ~RcServo() {}

        void read_settings();

        // Overrides for inherited methods
        void init() override;
        bool set_homing_mode(bool isHoming) override;
        void set_disable(bool disable) override;
        void update() override;

        void _write_pwm(uint32_t duty);

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {
            handler.item("output_pin", _output_pin);
            handler.item("pwm_hz", _pwm_freq, SERVO_PWM_FREQ_MIN, SERVO_PWM_FREQ_MAX);
            handler.item("min_pulse_us", _min_pulse_us, SERVO_PULSE_US_MIN, SERVO_PULSE_US_MAX);
            handler.item("max_pulse_us", _max_pulse_us, SERVO_PULSE_US_MIN, SERVO_PULSE_US_MAX);
            handler.item("timer_ms", _timer_ms);

            Servo::group(handler);
        }
    };
}
