// Copyright (c) 2022 -	Santiago Palomino
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	This is a PWM spindle driver without speed compensation for a full H-Bridge controller
   with support for two directions. The external HW has the following PINS:
     enable_pin : optional.
     output_cw_pin : Clockwise PWM signal
     output_ccw_pin : Counter Clockwise PWM signal
     When the output CW is toggling, CCW is set LOW, and viceversa.

     Features which could be added afterwards:

     Soft start to prevent unnecessary current spikes on the spindle power
     supply. The spindleDelay function seems to be a way to delay the
     application from using a spindle which has not accelerated yet. A similar
     feature could be added completing or replacing that functionality for
     this spindle type, or for all spindles.


*/

#include "Spindle.h"

#include <cstdint>

namespace Spindles {
    // This adds support for PWM H-Bridge Spindles
    class HBridge : public Spindle {
    public:
        HBridge(const char* name) : Spindle(name) {}

        // PWM(Pin&& output, Pin&& enable, Pin&& direction, uint32_t minRpm, uint32_t maxRpm) :
        //     _min_rpm(minRpm), _max_rpm(maxRpm), _output_pin(std::move(output)), _enable_pin(std::move(enable)),
        //     _direction_pin(std::move(direction)) {}

        HBridge(const HBridge&)            = delete;
        HBridge(HBridge&&)                 = delete;
        HBridge& operator=(const HBridge&) = delete;
        HBridge& operator=(HBridge&&)      = delete;

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
            // but the ESP32 hardware can handle them, so we let the
            // user choose.
            handler.item("pwm_hz", _pwm_freq, 1, 20000000);
            handler.item("output_cw_pin", _output_cw_pin);
            handler.item("output_ccw_pin", _output_ccw_pin);
            handler.item("enable_pin", _enable_pin);
            handler.item("disable_with_s0", _disable_with_zero_speed);

            Spindle::group(handler);
        }

        virtual ~HBridge() {}

    protected:
        // TODO: A/B rename
        int32_t      _current_pwm_duty;
        SpindleState _current_state      = SpindleState::Unknown;
        bool         _duty_update_needed = false;

        // _disable_with_zero_speed forces a disable when speed is 0
        bool _disable_with_zero_speed = false;

        // Clockwise is achieved setting PWM on the output_pin and LOW reverse_pin.
        // Counter Clockwise is achieved setting PWM on the reverse pin and LOW on output_pin
        Pin _enable_pin;
        Pin _output_cw_pin;
        Pin _output_ccw_pin;

        // Configurable
        uint32_t     _pwm_freq = 5000;
        SpindleState _state;
        void         set_enable(bool enable);

        void         set_output(uint32_t duty);
        virtual void deinit();
    };
}
