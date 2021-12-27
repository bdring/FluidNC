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

        pwm_cnt[SolenoidMode::Off]  = uint32_t(_off_percent / 100.0f * 65535.0f);
        pwm_cnt[SolenoidMode::Pull] = uint32_t(_pull_percent / 100.0f * 65535.0f);
        pwm_cnt[SolenoidMode::Hold] = uint32_t(_hold_percent / 100.0f * 65535.0f);

        log_info("    Solenoid Pin:" << _output_pin.name() << " Counts Off:" << _off_cnt << " Hold:" << _hold_cnt << " Pull:" << _pull_cnt);

        _axis_index = axis_index();

        config_message();

        _pwm_chan_num     = ledcInit(_output_pin, -1, double(_pwm_freq), SERVO_PWM_RESOLUTION_BITS);  // Allocate a channel
        _current_pwm_duty = 0;

        _disabled = true;

        startUpdateTask(_timer_ms);
    }

    void Solenoid::update() { set_location(); }

    void Solenoid::config_message() {
        log_info("    Solenoid Pin:" << _output_pin.name() << " Off:" << _off_percent << " Hold:" << _hold_percent
                                     << " Pull:" << _pull_percent << " Duration:" << _pull_ms);
    }

    void Solenoid::set_location() {
        bool is_solenoid_on;

        if (_disabled || _has_errors) {
            return;
        }

        float mpos     = steps_to_mpos(motor_steps[_axis_index], _axis_index);  // get the axis machine position in mm
        is_solenoid_on = (mpos > 0.0);                                          // TODO: we can apply an invert feature here if needed

        switch (_current_mode) {
            case SolenoidMode::Off:
                if (is_solenoid_on) {
                    _current_mode  = SolenoidMode::Pull;
                    _pull_off_time = 10;
                }
                break;
            case SolenoidMode::Pull:
                if (is_solenoid_on) {  // count down
                    if (_pull_off_time == 0) {
                        _current_mode = SolenoidMode::Hold;
                        break;
                    }
                    _pull_off_time--;
                } else {  // turn off
                    _current_mode = SolenoidMode::Off;
                }
                break;
            case SolenoidMode::Hold:
                if (!is_solenoid_on) {
                    _current_mode = SolenoidMode::Off;
                }
                break;
            default:
                break;
        }

        _write_pwm(pwm_cnt[_current_mode]);
    }

    namespace {
        MotorFactory::InstanceBuilder<Solenoid> registration("solenoid");
    }
}