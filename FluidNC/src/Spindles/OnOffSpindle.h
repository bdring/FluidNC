#pragma once

/*
	OnOffSpindle.h

	This is used for a basic on/off spindle All S Values above 0
	will turn the spindle on.

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
#include "Spindle.h"

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class OnOff : public Spindle {
    public:
        OnOff() = default;

        OnOff(const OnOff&) = delete;
        OnOff(OnOff&&)      = delete;
        OnOff& operator=(const OnOff&) = delete;
        OnOff& operator=(OnOff&&) = delete;

        void init() override;

        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        // Methods introduced by this base clase
        virtual void set_direction(bool Clockwise);
        void         set_enable(bool enable);

        // Configuration handlers:
        void validate() const override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("output_pin", _output_pin);
            handler.item("enable_pin", _enable_pin);
            handler.item("direction_pin", _direction_pin);
            handler.item("disable_with_zero_speed", _disable_with_zero_speed);
            handler.item("zero_speed_with_disable", _zero_speed_with_disable);

            Spindle::group(handler);
        }

        virtual ~OnOff() {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "OnOff"; }

    protected:
        Pin _enable_pin;
        Pin _output_pin;
        Pin _direction_pin;
        // _disable_with_zero_speed forces a disable when speed is 0
        bool _disable_with_zero_speed = false;
        // _zero_speed_with_disable forces speed to 0 when disabled
        bool _zero_speed_with_disable = true;

        virtual void set_output(uint32_t speed);
        virtual void deinit();
    };
}
