#pragma once

#include "RcServo.h"

namespace MotorDrivers {
    class Solenoid : public RcServo {
    protected:
        int32_t _timer_ms = 50;

        void config_message() override;
        void update() override;

        static constexpr int _update_rate_ms = 50;

        float    _off_percent  = 0.0;
        float    _pull_percent = 100.0;
        float    _hold_percent = 75.0;
        uint32_t _pull_ms      = 500;
        bool     _dir_invert   = false;

        uint32_t _pull_off_time = 0;  // When did the pull start

        enum SolenoidMode {
            Off  = 0,
            Pull = 1,
            Hold = 2,
        };

        uint32_t pwm_cnt[3];  // the pwm values in timer counts.

        SolenoidMode _current_mode = SolenoidMode::Off;

    public:
        Solenoid(const char* name) : RcServo(name) {}

        void set_location();
        void init() override;
        void set_disable(bool disable) override;

        float _transition_point;

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {
            // An on/off (or two-level pull/hold) solenoid actuator used as a virtual axis --
            // e.g. a pen-lift or pneumatic tool-drop mechanism.

            // @config output_pin
            // @default NO_PIN
            // PWM signal output driving the solenoid.
            handler.item("output_pin", _output_pin);

            // @config pwm_hz
            // @default 1000
            // PWM frequency driving the solenoid.
            handler.item("pwm_hz", _pwm_freq, 1000, 100000);

            // @config off_percent
            // @default 0.0
            // Duty cycle while off.
            handler.item("off_percent", _off_percent, 0.0f, 100.0f);

            // @config pull_percent
            // @default 100.0
            // Duty cycle during the initial pull-in (highest power, to overcome the
            // solenoid's resting inertia).
            handler.item("pull_percent", _pull_percent, 0.0f, 100.0f);

            // @config hold_percent
            // @default 75.0
            // Duty cycle after pull-in, while holding the solenoid engaged -- typically
            // lower than pull_percent, since holding a solenoid needs less power than
            // pulling it in.
            handler.item("hold_percent", _hold_percent, 0.0f, 100.0f);

            // @config pull_ms
            // @default 500
            // How long the pull_percent duty cycle is applied before switching to
            // hold_percent.
            handler.item("pull_ms", _pull_ms, 0, 3000);

            // @config direction_invert
            // @default false
            // Inverts which side of the axis's mpos 0.0 counts as "active".
            handler.item("direction_invert", _dir_invert);

            // @config timer_ms
            // @default 50
            // Update interval, in milliseconds, for the solenoid's PWM state machine
            // (pull/hold timing).
            handler.item("timer_ms", _timer_ms);

            Servo::group(handler);
        }
    };
}
