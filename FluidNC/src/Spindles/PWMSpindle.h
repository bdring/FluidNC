#pragma once

/*
	PWMSpindle.h

	This is a full featured TTL PWM spindle This does not include speed/power
	compensation. Use the Laser class for that.

	Part of Grbl_ESP32
	2020 -	Bart Dring

	Grbl is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	Grbl is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	You should have received a copy of the GNU General Public License
	along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

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
            handler.item("pwm_freq", _pwm_freq);

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
