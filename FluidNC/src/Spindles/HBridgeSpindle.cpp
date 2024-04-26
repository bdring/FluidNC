// Copyright (c) 2022 -	Santiago Palomino
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "HBridgeSpindle.h"

#include "../GCode.h"       // gc_state.modal
#include "../System.h"      // sys
#include "Driver/PwmPin.h"  // pwmInit(), etc.

namespace Spindles {
    void HBridge::init() {
        is_reversable = _output_ccw_pin.defined();

        setupSpeeds(_pwm_freq);

        if (_output_cw_pin.defined()) {
            if (_output_cw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                auto outputNative = _output_cw_pin.getNative(Pin::Capabilities::PWM);
                _pwm_cw           = new PwmPin(_output_cw_pin, _pwm_freq);
            } else {
                log_error(name() << " output_cw_pin " << _output_cw_pin.name().c_str() << " cannot do PWM");
            }
        } else {
            log_error(name() << " output_cw_pin not defined");
        }

        if (_output_ccw_pin.defined()) {
            if (_output_ccw_pin.capabilities().has(Pin::Capabilities::PWM)) {
                auto outputBNative = _output_ccw_pin.getNative(Pin::Capabilities::PWM);
                _pwm_ccw           = new PwmPin(_output_ccw_pin, _pwm_freq);
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
        setupSpeeds(_pwm_cw->period());
        config_message();
    }

    void IRAM_ATTR HBridge::set_enable(bool enable) {
        if (_disable_with_zero_speed && sys.spindle_speed == 0) {
            enable = false;
        }

        _enable_pin.synchronousWrite(enable);
    }

    void IRAM_ATTR HBridge::setSpeedfromISR(uint32_t dev_speed) {
        set_enable(gc_state.modal.spindle != SpindleState::Disable);
        set_output(dev_speed);
    }

    void HBridge::setState(SpindleState state, SpindleSpeed speed) {
        if (sys.abort) {
            return;  // Block during abort.
        }

        if (!_output_cw_pin.defined() || !_output_ccw_pin.defined()) {
            log_config_error(name() << " spindle pins not defined");
        }

        // We always use mapSpeed() with the unmodified input speed so it sets
        // sys.spindle_speed correctly.
        uint32_t dev_speed = mapSpeed(speed);
        if (state == SpindleState::Disable) {  // Halt or set spindle direction and speed.
            dev_speed = 0;
        } else {
            // The core is responsible for stopping the motor before changing
            // direction
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
    void HBridge::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out CW:" << _output_cw_pin.name()
                        << " Out CCW:" << _output_ccw_pin.name() << " Freq:" << _pwm_cw->frequency() << "Hz Period:" << _pwm_cw->period()

        );
    }

    void IRAM_ATTR HBridge::set_output(uint32_t duty) {
        if (!(_pwm_cw && _pwm_ccw)) {
            return;
        }

        // to prevent excessive calls to pwm->setDuty, make sure duty has changed
        if (duty == _current_pwm_duty && !_duty_update_needed) {
            return;
        }

        _duty_update_needed = false;

        _current_pwm_duty = duty;

        if (_state == SpindleState::Cw) {
            _pwm_cw->setDuty(0);
            _pwm_ccw->setDuty(duty);
        } else if (_state == SpindleState::Ccw) {
            _pwm_cw->setDuty(0);
            _pwm_ccw->setDuty(duty);
        } else {  // M5
            _pwm_cw->setDuty(0);
            _pwm_ccw->setDuty(0);
        }
    }

    void HBridge::deinit() {
        stop();
        if (_pwm_cw) {
            delete _pwm_cw;
            delete _pwm_ccw;
            _pwm_cw  = nullptr;
            _pwm_ccw = nullptr;
        }
        _output_cw_pin.setAttr(Pin::Attr::Input);
        _output_ccw_pin.setAttr(Pin::Attr::Input);
        _enable_pin.setAttr(Pin::Attr::Input);
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<HBridge> registration("HBridge");
    }
}
