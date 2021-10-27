// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is a full featured TTL PWM spindle This does not include speed/power
	compensation. Use the Laser class for that.
*/

#include "OnOffSpindle.h"

#include <cstdint>

namespace Spindles {
    // This adds support for PWM
    class PWM : public OnOff {
    public:
        PWM() = default;

        // PWM(Pin&& output, Pin&& enable, Pin&& direction, uint32_t minRpm, uint32_t maxRpm) :
        //     _min_rpm(minRpm), _max_rpm(maxRpm), _output_pin(std::move(output)), _enable_pin(std::move(enable)),
        //     _direction_pin(std::move(direction)) {}

        PWM(const PWM&) = delete;
        PWM(PWM&&)      = delete;
        PWM& operator=(const PWM&) = delete;
        PWM& operator=(PWM&&) = delete;

        void init() override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;
        // Configuration handlers:
        void validate() const override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("pwm_hz", _pwm_freq);

            OnOff::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "PWM"; }

        virtual ~PWM() {}

    protected:
        int32_t  _current_pwm_duty;
        int      _pwm_chan_num;
        uint32_t _pwm_period;     // how many counts in 1 period
        uint8_t  _pwm_precision;  // auto calculated

        // Configurable
        uint32_t _pwm_freq = 5000;

        void         set_output(uint32_t duty) override;
        virtual void deinit();

        virtual void get_pins_and_settings();
        uint8_t      calc_pwm_precision(uint32_t freq);
    };
}
