// Copyright (c) 2022 -	Santiago Palomino
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a full featured TTL PWM spindle This does not include speed/power
    compensation. Use the Laser class for that.
*/
#include "HBridgeSpindle.h"

#include "../GCode.h"  // gc_state.modal
#include "../Logging.h"
#include "../Pins/LedcPin.h"
#include <esp32-hal-ledc.h>  // ledcDetachPin

// ======================= PWM ==============================
/*
    This gets called at startup or whenever a spindle setting changes
    If the spindle is running it will stop and need to be restarted with M3Snnnn
*/

namespace Spindles {
    void HBridgeSpindle::init() {
        get_pins_and_settings();
        setupSpeeds(_pwm_freq);

        if (_output_cw_pin.defined()) {
            if (_output_cw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                auto outputNative = _output_cw_pin.getNative(Pin::Capabilities::PWM);
                _pwm_cw_chan_num  = ledcInit(_output_cw_pin, -1, (double)_pwm_freq, _pwm_precision);
            } else {
                log_error(name() << " output_cw_pin " << _output_cw_pin.name().c_str() << " cannot do PWM");
            }
        } else {
            log_error(name() << " output_cw_pin not defined");
        }

        if (_output_ccw_pin.defined()) {
            if (_output_ccw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                auto outputBNative = _output_ccw_pin.getNative(Pin::Capabilities::PWM);
                _pwm_ccw_chan_num  = ledcInit(_output_ccw_pin, -1, (double)_pwm_freq, _pwm_precision);
            } else {
                log_error(name() << " output_ccw_pin " << _output_ccw_pin.name().c_str() << " cannot do PWM");
            }
        } else {
            log_error(name() << " output_ccw_pin not defined");
        }

        _current_state    = SpindleState::Disable;
        _current_pwm_duty = 0;
        _enable_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            // The default speed map for a PWM spindle is linear from 0=0% to 10000=100%
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(_pwm_period);
        config_message();
    }

    // Get the GPIO from the machine definition
    void HBridgeSpindle::get_pins_and_settings() {
        // setup all the pins

        is_reversable = _output_ccw_pin.defined();

        _pwm_precision = calc_pwm_precision(_pwm_freq);  // determine the best precision
        _pwm_period    = (1 << _pwm_precision);
    }

    void IRAM_ATTR HBridgeSpindle::set_enable(bool enable) {
        if (_disable_with_zero_speed && sys.spindle_speed == 0) {
            enable = false;
        }

        _enable_pin.synchronousWrite(enable);
    }

    void IRAM_ATTR HBridgeSpindle::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    void HBridgeSpindle::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort) {
            return;  // Block during abort.
        }

        if (!_output_cw_pin.defined() || !_output_ccw_pin.defined()) {
            log_warn(name() << " spindle pins not defined");
        }

        // We always use mapSpeed() with the unmodified input speed so it sets
        // sys.spindle_speed correctly.
        uint32_t dev_speed = mapSpeed(speed);
        if (state == SpindleState::Disable) {  // Halt or set spindle direction and speed.
            dev_speed = 0;
        } else {
            // PWM: this could wreak havoc if the direction is changed without first
            // spinning down. But it looks like the framework is stopping the motor
            // before changing direction M4 is not accepted during M3 if M5 is not run first.
            if (state == SpindleState::Cw || state == SpindleState::Ccw) {}
        }
        _state = state;

        if (_current_state != state) {
            _current_state      = state;
            _duty_update_needed = true;
        }

        // rate adjusted spindles (laser) in M4 set power via the stepper engine, not here

        // set_output must go first because of the way enable is used for level
        // converters on some boards.
        if (isRateAdjusted() && (state == SpindleState::Ccw)) {
            dev_speed = offSpeed();
            set_output(dev_speed);
        } else {
            set_output(dev_speed);
        }

        set_enable(state != SpindleState::Disable);
        spindleDelay(state, speed);
    }

    // prints the startup message of the spindle config
    void HBridgeSpindle::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out CW:" << _output_cw_pin.name()
                        << " Out CCW:" << _output_ccw_pin.name() << " Freq:" << _pwm_freq << "Hz Res:" << _pwm_precision << "bits"

        );
    }

    void IRAM_ATTR HBridgeSpindle::set_output(uint32_t duty) {
        if (_pwm_cw_chan_num == -1 || _pwm_cw_chan_num == -1) {
            return;
        }

        // to prevent excessive calls to ledcSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty && !_duty_update_needed) {
            return;
        }

        _duty_update_needed = false;

        _current_pwm_duty = duty;

        if (_state == SpindleState::Cw) {
            ledcSetDuty(_pwm_cw_chan_num, 0);
            ledcSetDuty(_pwm_ccw_chan_num, duty);
        } else if (_state == SpindleState::Ccw) {
            ledcSetDuty(_pwm_ccw_chan_num, 0);
            ledcSetDuty(_pwm_cw_chan_num, duty);
        } else {  // M5
            ledcSetDuty(_pwm_cw_chan_num, 0);
            ledcSetDuty(_pwm_ccw_chan_num, 0);
        }
    }

    // Calculate the highest PWM precision in bits for the desired frequency
    // 80,000,000 (APB Clock) = freq * maxCount
    // maxCount is a power of two between 2^1 and 2^20
    // frequency is at most 80,000,000 / 2^1 = 40,000,000, limited elsewhere
    // to 20,000,000 to give a period of at least 2^2 = 4 levels of control.
    uint8_t HBridgeSpindle::calc_pwm_precision(uint32_t freq) {
        if (freq == 0) {
            freq = 1;  // Limited elsewhere but just to be safe...
        }

        // Increase the precision (bits) until it exceeds the frequency
        // The hardware maximum precision is 20 bits
        const uint8_t  ledcMaxBits = 20;
        const uint32_t apbFreq     = 80000000;
        const uint32_t maxCount    = apbFreq / freq;
        for (uint8_t bits = 2; bits <= ledcMaxBits; ++bits) {
            if ((1u << bits) > maxCount) {
                return bits - 1;
            }
        }
        return ledcMaxBits;
    }

    void HBridgeSpindle::deinit() {
        stop();
        ledcDetachPin(_output_cw_pin.getNative(Pin::Capabilities::PWM));
        ledcDetachPin(_output_ccw_pin.getNative(Pin::Capabilities::PWM));
        _output_cw_pin.setAttr(Pin::Attr::Input);
        _output_ccw_pin.setAttr(Pin::Attr::Input);
        _enable_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<HBridgeSpindle> registration("HBridgeSpindle");
    }
}
