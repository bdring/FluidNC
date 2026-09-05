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
            // @config output_pin
            // @default NO_PIN
            // @pin_attributes output
            // On/off (or PWM duty, depending on the concrete spindle type -- see that
            // type's own @pin_attributes_for output_pin override, e.g. PWMSpindle.h)
            // output signal. Turns off with M5.
            handler.item("output_pin", _output_pin);

            // @config enable_pin
            // @default NO_PIN
            // @pin_attributes output
            // Optional enable signal, separate from output_pin.
            handler.item("enable_pin", _enable_pin);

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
        IsrSpeedFn isr_speed_fn() override;
        void setState(SpindleState state, SpindleSpeed speed) override;
        void config_message() override;

        // Methods introduced by this base clase
        virtual void set_direction(bool Clockwise);
        virtual void set_enable(bool enable);

        // Configuration handlers:
        void validate() override { Spindle::validate(); }

        void group(Configuration::HandlerBase& handler) override {
            // @config direction_pin
            // @default NO_PIN
            // @pin_attributes output
            // Optional direction signal. M4 (spindle-reverse) is only accepted when a real
            // pin is assigned here -- without one, only M3/M5 are meaningful.
            handler.item("direction_pin", _direction_pin);

            // @default_for speed_map
            // @default 0=0% 1=100%
            // @default_note applied by OnOff::init() only when unset -- step function: 0 is off, any nonzero S is full on
            groupCommon(handler);
            Spindle::groupDelaySettings(handler);
        }

        virtual ~OnOff() {}

    protected:
        Pin _enable_pin;
        Pin _output_pin;
        Pin _direction_pin;

        virtual void set_output(uint32_t speed);
        virtual void deinit();
    };
}
