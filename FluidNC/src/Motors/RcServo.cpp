// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This lets an RcServo be used like any other motor. Servos
    have travel and speed limitations that must be respected.

    The servo's travel will be mapped against the axis with $X/MaxTravel

    The rotation can be inverted with by $Stepper/DirInvert

    Homing simply sets the axis Mpos to the endpoint as determined by $Homing/DirInvert

    Calibration is part of the setting (TBD) fixed at 1.00 now
*/

#include "RcServo.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"  // mpos_to_steps() etc
#include "../Pins/LedcPin.h"
#include "../Pin.h"
#include "../Limits.h"  // limitsMaxPosition
#include "RcServoSettings.h"

#include <esp32-hal-ledc.h>  // ledcWrite
#include <freertos/task.h>   // vTaskDelay

namespace MotorDrivers {
    // RcServo::RcServo(Pin pwm_pin) : Servo(), _pwm_pin(pwm_pin) {}

    void RcServo::init() {
        //_axis_index = axis_index();

        read_settings();
        _pwm_chan_num     = ledcInit(_output_pin, -1, _pwm_freq, SERVO_PULSE_RES_BITS);  // Allocate a channel
        _current_pwm_duty = 0;

        _disabled = true;
        config_message();
        //startUpdateTask(_timer_ms);
    }

    void RcServo::config_message() {
        log_info("    RC Servo Pin:" << _output_pin.name() << " Pulse Len(" << _min_pulse_us << "," << _max_pulse_us << ") "); //  << axisLimits());
    }

    void RcServo::_write_pwm(uint32_t duty) {
        // to prevent excessive calls to ledcWrite, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;
        ledcSetDuty(_pwm_chan_num, duty);
    }

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR RcServo::set_disable(bool disable) {
        _disabled = disable;
        if (_disabled) {
            _write_pwm(0);
        }
    }

    // Homing justs sets the new system position and the servo will move there
    bool RcServo::set_homing_mode(bool isHoming) {
        //auto axis                = config->_axes->_axis[_axis_index];
        //motor_steps[_axis_index] = mpos_to_steps(axis->_homing->_mpos, _axis_index);

        set_location();   // force the PWM to update now
        vTaskDelay(750);  // give time to move
        return false;     // Cannot be homed in the conventional way
    }

    void RcServo::update() { set_location(); }

    void RcServo::set_location() {
        uint32_t servo_pulse_len;
        float    servo_pos, mpos, offset;

        if (_disabled) {
            return;
        }

        read_settings();

        //mpos = steps_to_mpos(motor_steps[_axis_index], _axis_index);  // get the axis machine position in mm
        // TBD working in MPos
        offset    = 0;  // gc_state.coord_system[axis_index] + gc_state.coord_offset[axis_index];  // get the current axis work offset
        servo_pos = mpos - offset;  // determine the current work position

        // determine the pulse length
        // servo_pulse_len = static_cast<uint32_t>(
        //     mapConstrain(servo_pos, limitsMinPosition(_axis_index), limitsMaxPosition(_axis_index), _pwm_pulse_min, _pwm_pulse_max));

        //_write_pwm(servo_pulse_len);
    }

    void RcServo::read_settings() {

       // double servo_time_per_bit = ((1.0 / (double)_pwm_freq) / ((double)SERVO_PULSE_RES_COUNT));  // seconds

       uint16_t count_per_us = (uint16_t)(65535.0 * 50.0 / 1000.0);

       //_pwm_pulse_min = (uint16_t)(_min_pulse_us / 1000 / SERVO_TIME_PER_BIT);  // in timer counts
       //_pwm_pulse_max = (uint16_t)(_max_pulse_us / 1000 / SERVO_TIME_PER_BIT);  // in timer counts

    //    _pwm_pulse_min = count_per_us * _min_pulse_us;   // in timer counts
    //    _pwm_pulse_max = count_per_us * _max_pulse_us;   // in timer counts

    //    log_info("_pwm_pulse_min " << _pwm_pulse_min);
    //    log_info("_pwm_pulse_max " << _pwm_pulse_max);

    //    if (_invert_direction) {
    //        swap(_pwm_pulse_min, _pwm_pulse_max);
    //     }

    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<RcServo> registration("rc_servo");
    }
}
