// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is similar to the PWM Spindle except that it enables the
	M4 speed vs. power copensation.
*/

#include "PWMSpindle.h"

#include <cstdint>

namespace Spindles {
    // this is the same as a PWM spindle but the M4 compensation is supported.
    class Laser : public PWM {
    public:
        Laser(const char* name) : PWM(name) {};

        Laser(const Laser&)            = delete;
        Laser(Laser&&)                 = delete;
        Laser& operator=(const Laser&) = delete;
        Laser& operator=(Laser&&)      = delete;

        bool isRateAdjusted() override;
        void config_message() override;
        void init() override;
        void set_direction(bool Clockwise) override {};

        void group(Configuration::HandlerBase& handler) override {
            // pwm_freq is the only item that the PWM class adds to OnOff
            // We cannot call PWM::group() because that would pick up
            // direction_pin, which we do not want in Laser

            // @config pwm_hz
            // @default 5000
            // PWM signal frequency, same field as PWMSpindle's pwm_hz but with a narrower
            // range here (1000-100000 rather than 1-20000000).
            handler.item("pwm_hz", _pwm_freq, 1000, 100000);
            OnOff::groupCommon(handler);
        }

        ~Laser() {}
    };
}
