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
        Laser() = default;

        Laser(const Laser&) = delete;
        Laser(Laser&&)      = delete;
        Laser& operator=(const Laser&) = delete;
        Laser& operator=(Laser&&) = delete;

        bool isRateAdjusted() override;
        void config_message() override;
        void get_pins_and_settings() override;
        void set_direction(bool Clockwise) override {};
        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Laser"; }

        void group(Configuration::HandlerBase& handler) override { PWM::group(handler); }

        ~Laser() {}
    };
}
