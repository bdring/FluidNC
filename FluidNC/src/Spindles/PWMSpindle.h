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
    class PWM : public OnOff {
    public:
        PWM(const char* name) : OnOff(name) {}

        // PWM(Pin&& output, Pin&& enable, Pin&& direction, uint32_t minRpm, uint32_t maxRpm) :
        //     _min_rpm(minRpm), _max_rpm(maxRpm), _output_pin(std::move(output)), _enable_pin(std::move(enable)),
        //     _direction_pin(std::move(direction)) {}

        PWM(const PWM&)            = delete;
        PWM(PWM&&)                 = delete;
        PWM& operator=(const PWM&) = delete;
        PWM& operator=(PWM&&)      = delete;

        void init() override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;
        // Configuration handlers:
        void validate() override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            // The APB clock frequency is 80MHz and the maximum divisor
            // is 2^10.  The maximum precision is 2^20. 80MHz/2^(20+10)
            // is 0.075 Hz, or one cycle in 13.4 seconds.  We cannot
            // represent that in an integer so we set the minimum
            // frequency to 1 Hz.  Frequencies of 76 Hz or less use
            // the full 20 bit resolution, 77 to 152 Hz uses 19 bits,
            // 153 to 305 uses 18 bits, ...
            // At the other end, the minimum useful precision is 2^2
            // or 4 levels of control, so the max is 80MHz/2^2 = 20MHz.
            // Those might not be practical for many CNC applications,
            // but the hardware can handle them, so we let the
            // user choose.
            handler.item("pwm_hz", _pwm_freq, 1, 20000000);

            OnOff::group(handler);
        }

        virtual ~PWM() {}

    protected:
        uint32_t _current_pwm_duty = 0;

        // Configurable
        uint32_t _pwm_freq = 5000;

        void         set_output(uint32_t duty) override;
        virtual void deinit();
    };
}
