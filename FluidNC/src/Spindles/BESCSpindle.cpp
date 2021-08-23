// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    BESCSpindle.cpp

    This a special type of PWM spindle for RC type Brushless DC Speed
    controllers. They use a short pulse for off and a longer pulse for
    full on. The pulse is always a small portion of the full cycle.
    Some BESCs have a special turn on procedure. This may be a one time
    procedure or must be done every time. The user must do that via gcode.

    Important ESC Settings
    50 Hz this is a typical frequency for an ESC
    Some ESCs can handle higher frequencies, but there is no advantage to changing it.

    Determine the typical min and max pulse length of your ESC
    _min_pulse_secs is typically 1ms (0.001 sec) or less
    _max_pulse_secs is typically 2ms (0.002 sec) or more

*/
#include "BESCSpindle.h"

#include "../Pins/LedcPin.h"

#include <soc/ledc_struct.h>

namespace Spindles {
    void BESC::init() {
        if (_output_pin.undefined()) {
            log_warn("BESC output pin not defined");
            return;  // We cannot continue without the output pin
        }

        is_reversable = _direction_pin.defined();

        // override some settings in the PWM base class to what is required for a BESC
        _pwm_freq      = besc_pwm_freq;
        _pwm_precision = 16;
        _pwm_period    = (1 << _pwm_precision);

        _pwm_chan_num = ledcInit(_output_pin, -1, double(_pwm_freq), _pwm_precision);  // allocate and setup a PWM channel

        _enable_pin.setAttr(Pin::Attr::Output);

        // BESC PWM typically represents 0 speed as a 1ms pulse and max speed as a 2ms pulse

        // 1000000 is us/sec
        const uint32_t pulse_period_us = 1000000 / besc_pwm_freq;

        // Calculate the pulse length offset and scaler in counts of the LEDC controller
        _min_pulse_counts  = (_min_pulse_us << _pwm_precision) / pulse_period_us;
        _pulse_span_counts = ((_max_pulse_us - _min_pulse_us) << _pwm_precision) / pulse_period_us;

        if (_speeds.size() == 0) {
            shelfSpeeds(4000, 20000);
        }

        // We set the dev_speed scale in the speed map to the full PWM period (64K)
        // Then, in set_output, we map the dev_speed range of 0..64K to the pulse
        // length range of ~1ms .. 2ms
        setupSpeeds(_pwm_period);

        stop();
        config_message();
    }

    void IRAM_ATTR BESC::set_output(uint32_t duty) {
        if (_output_pin.undefined()) {
            return;
        }

        // to prevent excessive calls to ledcSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;

        // This maps the dev_speed range of 0..(1<<_pwm_precision) into the pulse length
        // where _min_pulse_counts represents off and (_min_pulse_counts + _pulse_span_counts)
        // represents full on.  Typically the off value is a 1ms pulse length and the
        // full on value is a 2ms pulse.
        uint32_t pulse_counts = _min_pulse_counts + ((duty * _pulse_span_counts) >> _pwm_precision);

        ledcSetDuty(_pwm_chan_num, pulse_counts);
    }

    // prints the startup message of the spindle config
    void BESC::config_message() {
        log_info(name() << " Spindle Out:" << _output_pin.name() << " Min:" << _min_pulse_us << "us Max:" << _max_pulse_us
                        << "us Freq:" << _pwm_freq << "Hz Res:" << _pwm_precision << "bits");
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<BESC> registration("BESC");
    }
}
