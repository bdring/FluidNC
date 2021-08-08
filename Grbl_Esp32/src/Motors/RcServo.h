#pragma once

/*
    RcServo.h

    Part of Grbl_ESP32

    2020 -	Bart Dring

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Servo.h"

namespace MotorDrivers {
    class RcServo : public Servo {
    protected:
        void config_message() override;

        void set_location();

        Pin      _pwm_pin;
        uint8_t  _channel_num;
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
