// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Servo.h"
#include "RcServoSettings.h"
#include "System.h"

namespace MotorDrivers {
    class RcServo : public Servo {
    protected:
        int32_t _timer_ms = 20;

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

        steps_t _min_steps;
        steps_t _max_steps;

        axis_t _axis = INVALID_AXIS;

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
            handler.item("pwm_hz", _pwm_freq, SERVO_PWM_FREQ_MIN, SERVO_PWM_FREQ_MAX);

            // @config min_pulse_us
            // @default 1000
            // @tuning per-machine
            // Pulse width, in microseconds, corresponding to one end of the servo's travel.
            handler.item("min_pulse_us", _min_pulse_us, SERVO_PULSE_US_MIN, SERVO_PULSE_US_MAX);

            // @config max_pulse_us
            // @default 2000
            // @tuning per-machine
            // Pulse width, in microseconds, corresponding to the other end of the servo's
            // travel.
            handler.item("max_pulse_us", _max_pulse_us, SERVO_PULSE_US_MIN, SERVO_PULSE_US_MAX);

            // @config timer_ms
            // @default 20
            // @tuning typical
            // Update interval, in milliseconds, for refreshing the servo's PWM position.
            handler.item("timer_ms", _timer_ms, TIMER_MS_MIN, TIMER_MS_MAX);

            Servo::group(handler);
        }
    };
}
