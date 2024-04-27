// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an RcServo be used like any other motor. Servos
    have travel and speed limitations that must be respected.

    The servo's travel will be mapped against the axis with $X/MaxTravel

    The rotation can be inverted by swapping the values
    min_pulse_us: 2100
    max_pulse_us: 1000

    Homing simply sets the axis Mpos to the endpoint as determined by homing/mpos

*/

#include "RcServo.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"  // mpos_to_steps() etc
#include "../Pin.h"
#include "../Limits.h"  // limitsMaxPosition
#include "RcServoSettings.h"

#include <freertos/task.h>  // vTaskDelay

namespace MotorDrivers {
    // RcServo::RcServo(Pin pwm_pin) : Servo(), _pwm_pin(pwm_pin) {}

    void RcServo::init() {
        if (_output_pin.undefined()) {
            log_config_error("    RC Servo disabled: No output pin");
            _has_errors = true;
            return;  // We cannot continue without the output pin
        }

        _axis_index = axis_index();

        _pwm = new PwmPin(_output_pin, _pwm_freq);  // Allocate a channel

        _current_pwm_duty = 0;

        read_settings();

        config_message();

        _disabled = true;

        schedule_update(this, _timer_ms);
    }

    void RcServo::config_message() {
        log_info("    " << name() << " Pin:" << _output_pin.name() << " Pulse Len(" << _min_pulse_us << "," << _max_pulse_us
                        << " period:" << _pwm->period() << ")");
    }

    void RcServo::_write_pwm(uint32_t duty) {
        // to prevent excessive calls to pwmSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;
        _pwm->setDuty(duty);
    }

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR RcServo::set_disable(bool disable) {
        //log_info("Set dsbl " << disable);
        if (_has_errors)
            return;

        _disabled = disable;
        if (_disabled) {
            _write_pwm(0);
        }
    }

    // Homing justs sets the new system position and the servo will move there
    bool RcServo::set_homing_mode(bool isHoming) {
        log_debug("Servo homing:" << isHoming);
        if (_has_errors)
            return false;

        if (isHoming) {
            auto axis = config->_axes->_axis[_axis_index];
            set_motor_steps(_axis_index, mpos_to_steps(axis->_homing->_mpos, _axis_index));

            float home_time_sec = (axis->_maxTravel / axis->_maxRate * 60 * 1.1);  // 1.1 fudge factor for accell time.

            _disabled = false;
            set_location();                    // force the PWM to update now
            vTaskDelay(home_time_sec * 1000);  // give time to move
        }
        return false;  // Cannot be homed in the conventional way
    }

    void RcServo::update() { set_location(); }

    void RcServo::set_location() {
        if (_disabled || _has_errors) {
            return;
        }

        uint32_t servo_pulse_len;
        float    servo_pos;

        read_settings();

        float mpos = steps_to_mpos(get_axis_motor_steps(_axis_index), _axis_index);  // get the axis machine position in mm
        servo_pos  = mpos;                                                           // determine the current work position

        // determine the pulse length
        servo_pulse_len = static_cast<uint32_t>(mapConstrain(
            servo_pos, limitsMinPosition(_axis_index), limitsMaxPosition(_axis_index), (float)_min_pulse_cnt, (float)_max_pulse_cnt));

        // log_info("su " << servo_pulse_len);

        _write_pwm(servo_pulse_len);
    }

    void RcServo::read_settings() {
        _min_pulse_cnt = (_min_pulse_us * ((_pwm_freq * _pwm->period()) / 1000)) / 1000;  // play some math games to prevent overflowing 32 bit
        _max_pulse_cnt = (_max_pulse_us * ((_pwm_freq * _pwm->period()) / 1000)) / 1000;
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<RcServo> registration("rc_servo");
    }
}
