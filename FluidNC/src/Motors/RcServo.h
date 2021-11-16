// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Servo.h"
#include "RcServoSettings.h"

namespace MotorDrivers {
    class RcServo : public Servo {
    protected:
        void config_message() override;

        void set_location();

        Pin      _output_pin;
        uint32_t _pwm_freq = SERVO_PWM_FREQ_DEFAULT;  // 50 Hz
        uint8_t  _pwm_chan_num;
        uint32_t _current_pwm_duty;

        bool _disabled;

        uint32_t _min_pulse_us = SERVO_PULSE_US_MIN_DEFAULT;  // microseconds
        uint32_t _max_pulse_us = SERVO_PULSE_US_MAX_DEFAULT;  // microseconds

        uint32_t _min_pulse_cnt = 0;  // microseconds
        uint32_t _max_pulse_cnt = 0;  // microseconds

        int _axis_index = -1;

        bool _has_errors = false;

    public:
        RcServo() = default;

        // Overrides for inherited methods
        void init() override;
        void read_settings() override;
        bool set_homing_mode(bool isHoming) override;
        void set_disable(bool disable) override;
        void update() override;

        void _write_pwm(uint32_t duty);

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {
            handler.item("output_pin", _output_pin);
            handler.item("pwm_hz", _pwm_freq);
            handler.item("min_pulse_us", _min_pulse_us);
            handler.item("max_pulse_us", _max_pulse_us);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "rc_servo"; }
    };
}
