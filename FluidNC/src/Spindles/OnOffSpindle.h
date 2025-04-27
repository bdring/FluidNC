// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is used for a basic on/off spindle All S Values above 0
	will turn the spindle on.
*/

#include "Spindle.h"

namespace Spindles {
    // This is for an on/off spindle all RPMs above 0 are on
    class OnOff : public Spindle {
    protected:
        // This includes all items except direction_pin.  direction_pin applies
        // to most but not all of OnOff's derived classes.  Derived classes that
        // do not support direction_pin can invoke OnOff::groupCommon() instead
        // of OnOff::group()
        void groupCommon(Configuration::HandlerBase& handler) {
            handler.item("output_pin", _output_pin);
            handler.item("enable_pin", _enable_pin);
            handler.item("disable_with_s0", _disable_with_zero_speed);

            Spindle::group(handler);
        }

    public:
        OnOff(const char* name) : Spindle(name) {}

        OnOff(const OnOff&)            = delete;
        OnOff(OnOff&&)                 = delete;
        OnOff& operator=(const OnOff&) = delete;
        OnOff& operator=(OnOff&&)      = delete;

        void init() override;

        void setSpeedfromISR(uint32_t dev_speed) override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        // Methods introduced by this base clase
        virtual void set_direction(bool Clockwise);
        virtual void set_enable(bool enable);

        // Configuration handlers:
        void validate() override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            handler.item("direction_pin", _direction_pin);
            groupCommon(handler);
        }

        virtual ~OnOff() {}

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
