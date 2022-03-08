// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a full featured TTL PWM spindle This does not include speed/power
    compensation. Use the Laser class for that.
*/
#include "PWMSpindle.h"

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
    void PWM::init() {
        get_pins_and_settings();
        setupSpeeds(_pwm_freq);

        if (_output_pin.undefined()) {
            log_warn(name() << " output pin not defined");
            return;  // We cannot continue without the output pin
        }

        if (!_output_pin.capabilities().has(Pin::Capabilities::PWM)) {
            log_warn(name() << " output pin " << _output_pin.name().c_str() << " cannot do PWM");
            return;
        }

        

        _current_state    = SpindleState::Disable;
        _current_pwm_duty = 0;

        auto outputNative = _output_pin.getNative(Pin::Capabilities::PWM);

        _pwm_chan_num = ledcInit(_output_pin, -1, (double)_pwm_freq, _pwm_precision);

        _enable_pin.setAttr(Pin::Attr::Output);
        _direction_pin.setAttr(Pin::Attr::Output);

        if (_speeds.size() == 0) {
            // The default speed map for a PWM spindle is linear from 0=0% to 10000=100%
            linearSpeeds(10000, 100.0f);
        }
        setupSpeeds(_pwm_period);

        
        if (_use_pwm_ramping) {
            if (maxSpeed() < 500 || _spinup_ms < 500 || _spindown_ms < 500) {  // Some reasonable values for ramping
                log_warn("PWM Ramping max speed < 500 or spinup_ms/spindown_ms < 500...disabling");
                _use_pwm_ramping = false;
            } else {
                // TODO: Do we want to deal with a min speed?
                _ramp_up_dev_increment   = mapSpeed(maxSpeed()) / (_spinup_ms / _ramp_interval);
                _ramp_down_dev_increment = mapSpeed(maxSpeed()) / (_spindown_ms / _ramp_interval);
                log_info("PWM Ramping Maxspeed:" << maxSpeed() << " spinup incr:" << _ramp_up_dev_increment
                                                 << " spindown incr:" << _ramp_down_dev_increment);
            }
        }

        log_info("Maxspeed:" << maxSpeed() << " mapped max:" << mapSpeed(maxSpeed()) << " ovr:" << sys.spindle_speed_ovr);

        config_message();
    }

    // Get the GPIO from the machine definition
    void PWM::get_pins_and_settings() {
        // setup all the pins

        is_reversable = _direction_pin.defined();

        _pwm_precision = calc_pwm_precision(_pwm_freq);  // determine the best precision
        _pwm_period    = (1 << _pwm_precision);
    }

    void IRAM_ATTR PWM::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    // XXX this is the same as OnOff::setState so it might be possible to combine them
    void PWM::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort) {
            return;  // Block during abort.
        }

        //log_info("Spindle setState:" << state_name() << " Speed:" << speed);

        if (_use_pwm_ramping) {
            set_enable(state != SpindleState::Disable);
            if (state != SpindleState::Disable) {
                if (_direction_pin.defined() && (_direction_pin.read() != (state == SpindleState::Cw))) {
                    ramp_speed(0);
                }
                set_direction(state == SpindleState::Cw);
                ramp_speed(speed);                
            } else {
                ramp_speed(0);  // Always want to ramp down on diable
            }
        } else {
            // We always use mapSpeed() with the unmodified input speed so it sets
            // sys.spindle_speed correctly.
            uint32_t dev_speed = mapSpeed(speed);
            if (state == SpindleState::Disable) {  // Halt or set spindle direction and speed.
                if (_zero_speed_with_disable) {
                    dev_speed = offSpeed();
                }
            } else {
                // XXX this could wreak havoc if the direction is changed without first
                // spinning down.
                set_direction(state == SpindleState::Cw);
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
    }

    // prints the startup message of the spindle config
    void PWM::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Dir:" << _direction_pin.name()
                        << " Freq:" << _pwm_freq << "Hz Res:" << _pwm_precision << "bits"

        );
    }

    void IRAM_ATTR PWM::set_output(uint32_t duty) {
        if (_output_pin.undefined()) {
            return;
        }

        // to prevent excessive calls to ledcSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;

        ledcSetDuty(_pwm_chan_num, duty);
    }

    // Calculate the highest PWM precision in bits for the desired frequency
    // 80,000,000 (APB Clock) = freq * maxCount
    // maxCount is a power of two between 2^1 and 2^20
    // frequency is at most 80,000,000 / 2^1 = 40,000,000, limited elsewhere
    // to 20,000,000 to give a period of at least 2^2 = 4 levels of control.
    uint8_t PWM::calc_pwm_precision(uint32_t freq) {
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

    void PWM::deinit() {
        stop();
        ledcDetachPin(_output_pin.getNative(Pin::Capabilities::PWM));
        _output_pin.setAttr(Pin::Attr::Input);
        _enable_pin.setAttr(Pin::Attr::Input);
        _direction_pin.setAttr(Pin::Attr::Input);
    }

    void PWM::ramp_speed(uint32_t target_rpm) {
        // speed is given, but we need to work in dev_speed
        uint32_t target_duty = mapSpeed(target_rpm);
        uint32_t next_duty = _current_duty; // this is the value that increments in this function
        bool spinup = (target_duty > _current_duty);

        //log_info("Ramp duty from:" << _current_duty << " to:" << target_duty);

        while ((spinup && next_duty < target_duty) || (!spinup && (next_duty > target_duty))) {
            if (spinup) {
                if (next_duty + _ramp_up_dev_increment < target_duty) {
                    next_duty += _ramp_up_dev_increment;
                } else {
                    next_duty = target_duty;
                }
            } else {
                if ((next_duty > _ramp_down_dev_increment) && (next_duty - _ramp_down_dev_increment > target_duty)) {  // is is safe to subtract?
                    next_duty -= _ramp_down_dev_increment;
                } else {
                    next_duty = target_duty;
                }
            }

            set_output(next_duty);
            _current_duty = next_duty;
            if (next_duty == target_duty) {
                return;
            }
            delay_ms(_ramp_interval);
        }
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<PWM> registration("PWM");
    }
}
