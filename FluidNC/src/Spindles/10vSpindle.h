// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	10vSpindle.h

	This is basically a PWM spindle with some changes, so a separate forward and
	reverse signal can be sent.

	The direction pins will act as enables for the 2 directions. There is usually
	a min RPM with VFDs, that speed will remain even if speed is 0. You
	must turn off both direction pins when enable is off.
*/

#include "PWMSpindle.h"

namespace Spindles {
    class _10v : public PWM {
    public:
        _10v() = default;

        _10v(const _10v&) = delete;
        _10v(_10v&&)      = delete;
        _10v& operator=(const _10v&) = delete;
        _10v& operator=(_10v&&) = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;

        void deinit() override;

        // Configuration handlers:
        void validate() override { PWM::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("forward_pin", _forward_pin);
            handler.item("reverse_pin", _reverse_pin);
            PWM::group(handler);
        }

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "10V"; }

        ~_10v() {}

    protected:
        void set_enable(bool enable_pin) override;
        void set_direction(bool Clockwise);

        Pin _forward_pin;
        Pin _reverse_pin;
    };
}
