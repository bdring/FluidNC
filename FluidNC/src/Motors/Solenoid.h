#pragma once

#include "RcServo.h"

namespace MotorDrivers {
    class Solenoid : public RcServo {
    protected:
        void config_message() override;
        void update() override;

        float    _off_percent  = 0.0;
        float    _pull_percent = 100.0;
        float    _hold_percent = 75.0;
        uint32_t _pull_ms      = 150;

        uint32_t _pull_off_time = 0;  // When did the pull start

        enum SolenoidMode {
            Off  = 0,
            Pull = 1,
            Hold = 2,
        };

        uint32_t pwm_cnt[3];

        SolenoidMode _current_mode = SolenoidMode::Off;

    public:
        Solenoid() = default;

        void set_location();
        void init() override;

        float _transition_point;

        // Configuration handlers:
        void group(Configuration::HandlerBase& handler) override {
            handler.item("output_pin", _output_pin);
            handler.item("pwm_hz", _pwm_freq);
            handler.item("off_percent", _off_percent);
            handler.item("pull_percent", _pull_percent);
            handler.item("hold_percent", _hold_percent);
            handler.item("pull_ms", _pull_ms);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "solenoid"; }
    };
}
