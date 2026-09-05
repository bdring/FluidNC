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
        _10v(const char* name) : PWM(name) {}

        _10v(const _10v&)            = delete;
        _10v(_10v&&)                 = delete;
        _10v& operator=(const _10v&) = delete;
        _10v& operator=(_10v&&)      = delete;

        void init() override;
        void config_message() override;
        void setSpeedfromISR(uint32_t dev_speed) override;
        IsrSpeedFn isr_speed_fn() override;

        void deinit() override;

        // Configuration handlers:
        void validate() override { PWM::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            // Designed for controllers with a 0-10V analog control input plus separate
            // forward/reverse direction pins. The ESP32 can't generate 0-10V directly, but
            // some FluidNC controllers include a hardware adapter that produces it from a
            // PWM-driven GPIO; the plain PWM spindle type can drive the same adapter too,
            // but without separate direction pins -- use this type only when that specific
            // direction-pin wiring is needed.

            // @config forward_pin
            // @default NO_PIN
            // @pin_attributes output
            // Signals forward rotation when using separate forward/reverse pins. May
            // remain on after M5; turns off after M4.
            handler.item("forward_pin", _forward_pin);

            // @config reverse_pin
            // @default NO_PIN
            // @pin_attributes output
            // Signals reverse rotation when using separate forward/reverse pins. May
            // remain on after M5; turns off after M3.
            handler.item("reverse_pin", _reverse_pin);

            // @default_for speed_map
            // @default 0=0% 0=30% 6000=30% 20000=100%
            // @default_note applied by _10v::init() only when unset -- shelf: flat 30% to 6000, then linear to 20000=100%
            PWM::group(handler);
        }

        ~_10v() {}

    protected:
        void set_enable(bool enable_pin) override;
        void set_direction(bool Clockwise) override;

        Pin _forward_pin;
        Pin _reverse_pin;
    };
}
