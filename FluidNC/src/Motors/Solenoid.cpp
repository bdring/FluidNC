// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an Solenoid act like an axis

*/

#include "Solenoid.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"  // mpos_to_steps() etc
#include "../Pins/LedcPin.h"
#include "../Pin.h"
#include "../Limits.h"  // limitsMaxPosition
#include "../NutsBolts.h"

#include <esp32-hal-ledc.h>  // ledcWrite
#include <freertos/task.h>   // vTaskDelay

namespace MotorDrivers {

    void Solenoid::init() {
        constrain_with_message(_pwm_freq, (uint32_t)1000, (uint32_t)10000);
        constrain_with_message(_off_percent, 0.0f, 100.0f);
        constrain_with_message(_pull_percent, 0.0f, 100.0f);
        constrain_with_message(_hold_percent, 0.0f, 100.0f);
        constrain_with_message(_pull_ms, (uint32_t)0, (uint32_t)3000);

        if (_output_pin.undefined()) {
            log_warn("    Solenoid disabled: No output pin");
            _has_errors = true;
            return;  // We cannot continue without the output pin
        }

        _off_cnt  = uint32_t(_off_percent / 100.0f * 65535.0f);
        _pull_cnt = uint32_t(_pull_percent / 100.0f * 65535.0f);
        _hold_cnt = uint32_t(_hold_percent / 100.0f * 65535.0f);

        log_info("    Solenoid Pin:" << _output_pin.name() << " Counts Off:" << _off_cnt << " Hold:" << _hold_cnt << " Pull:" << _pull_cnt);

        _axis_index = axis_index();

        //read_settings();
        config_message();

        _pwm_chan_num     = ledcInit(_output_pin, -1, double(_pwm_freq), SERVO_PWM_RESOLUTION_BITS);  // Allocate a channel
        _current_pwm_duty = 0;

        _disabled = true;

        startUpdateTask(_timer_ms);
    }

    void Solenoid::config_message() {
        log_info("    Solenoid Pin:" << _output_pin.name() << " Off:" << _off_percent << " Hold:" << _hold_percent
                                     << " Pull:" << _pull_percent << " Duration:" << _pull_ms);
    }

    void Solenoid::set_location() {
        static bool _was_positive = false;
        uint32_t    _pwm_val      = 0;

        if (_disabled || _has_errors) {
            return;
        }

        float mpos = steps_to_mpos(motor_steps[_axis_index], _axis_index);  // get the axis machine position in mm

        if (mpos > 0.0) {
            if (_was_positive) {
                if (_current_mode == Pull && getCpuTicks() > _pull_off_time) {
                    _pwm_val      = _hold_cnt;
                    _current_mode = Hold;
                }
                return;
            } else {
                _pull_off_time = getCpuTicks() + _pull_ms * 1000;  // when should the pull turn off?
                _pwm_val       = _pull_cnt;
            }
        } else {
            if (!_was_positive) {
                return;
            }
            _pwm_val      = _off_cnt;
            _current_mode = Off;
        }

        _write_pwm(_pwm_val);
    }

    namespace {
        MotorFactory::InstanceBuilder<Solenoid> registration("solenoid");
    }
}