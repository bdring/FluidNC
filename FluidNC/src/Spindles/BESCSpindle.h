// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	BESCSpindle.h

	This a special type of PWM spindle for RC type Brushless DC Speed
	controllers. They use a short pulse for off and a longer pulse for
	full on. The pulse is always a small portion of the full cycle.
	Some BESCs have a special turn on procedure. This may be a one time
	procedure or must be done every time. The user must do that via gcode.

	Important ESC Settings
	50 Hz this is a typical frequency for an ESC
	Some ESCs can handle higher frequencies, but there is no advantage to changing it.

	Determine the typical min and max pulse length of your ESC
	BESC_MIN_PULSE_SECS is typically 1ms (0.001 sec) or less
	BESC_MAX_PULSE_SECS is typically 2ms (0.002 sec) or more

*/

#include "PWMSpindle.h"

#include <cstdint>

namespace Spindles {
    class BESC : public PWM {
    private:
        // Fixed
        static constexpr uint32_t besc_pwm_min_freq = 50;    // 50 Hz
        static constexpr uint32_t besc_pwm_max_freq = 2000;  // 50 Hz

        // Calculated
        uint32_t _pulse_span_counts;  // In counts of a 32-bit counter. ESP32 uses up to 20bits
        uint32_t _min_pulse_counts;   // In counts of a 32-bit counter  ESP32 uses up to 20bits

    protected:
        // Configurable
        uint32_t _min_pulse_us = 900;   // microseconds
        uint32_t _max_pulse_us = 2200;  // microseconds

    public:
        BESC() = default;

        BESC(const BESC&) = delete;
        BESC(BESC&&)      = delete;
        BESC& operator=(const BESC&) = delete;
        BESC& operator=(BESC&&) = delete;

        void init() override;
        void config_message() override;

        void set_output(uint32_t duty) override;

        // Configuration handlers:
        void validate() override { PWM::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            PWM::group(handler);

            handler.item("min_pulse_us", _min_pulse_us, 500, 3000);
            handler.item("max_pulse_us", _max_pulse_us, 500, 3000);
        }

        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "BESC"; }

        ~BESC() {}
    };
}
