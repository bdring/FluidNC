// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Servo.h"

namespace MotorDrivers {
    class RcServo : public Servo {
    protected:
        void config_message() override;

        void set_location();

        Pin      _pwm_pin;
        uint8_t  _pwm_chan_num;
        uint32_t _current_pwm_duty;
        float    _homing_position;
        bool     _invert_direction = false;

        float _pwm_pulse_min;
        float _pwm_pulse_max;

        bool _disabled;

        float _cal_min;
        float _cal_max;

        int _axis_index = -1;

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
        void validate() const override { Assert(!_pwm_pin.undefined(), "PWM pin should be configured."); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("pwm", _pwm_pin);
            handler.item("cal_min", _cal_min);
            handler.item("cal_max", _cal_max);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "rc_servo"; }
    };
}
