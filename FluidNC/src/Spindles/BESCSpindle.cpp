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

#include "Driver/PwmPin.h"  // pwmInit(), etc.

namespace Spindles {
    void BESC::init() {
        if (_output_pin.undefined()) {
            log_config_error(name() << " spindle output pin not defined");
            return;  // We cannot continue without the output pin
        }

        is_reversable = _direction_pin.defined();

        // override some settings in the PWM base class to what is required for a BESC
        constrain_with_message(_pwm_freq, besc_pwm_min_freq, besc_pwm_max_freq, "pwm_freq");

        _pwm = new PwmPin(_output_pin, _pwm_freq);  // allocate and setup a PWM channel

        _enable_pin.setAttr(Pin::Attr::Output);

        // BESC PWM typically represents 0 speed as a 1ms pulse and max speed as a 2ms pulse

        // 1000000 is us/sec
        const uint32_t pulse_period_us = 1000000 / _pwm->frequency();

        // Calculate the pulse length offset and scaler in counts of the PWM controller
        _min_pulse_counts  = (_min_pulse_us * _pwm->period()) / pulse_period_us;
        _pulse_span_counts = ((_max_pulse_us - _min_pulse_us) * _pwm->period()) / pulse_period_us;

        if (_speeds.size() == 0) {
            shelfSpeeds(4000, 20000);
        }

        // Use yaml speed_map to setup speed map for "spindle speed" conversion to timer counts used by PWM controller
        //setupSpeeds(_pulse_span_counts); // Map the counts for just the part of the pulse that changes to keep math inside 32bits later...
        setupSpeeds(_pwm->period());  // Map the entire pulse width period in counts
        stop();
        config_message();
    }

    void IRAM_ATTR BESC::set_output(uint32_t duty) {
        if (_output_pin.undefined()) {
            return;
        }

        // to prevent excessive calls to pwmSetDuty, make sure duty has changed
        if (duty == _current_pwm_duty) {
            return;
        }

        _current_pwm_duty = duty;

        // This maps the dev_speed range of 0..(1<<_pwm_precision) into the pulse length
        // where _min_pulse_counts represents off and (_min_pulse_counts + _pulse_span_counts)
        // represents full on.  Typically the off value is a 1ms pulse length and the
        // full on value is a 2ms pulse.
        // uint32_t pulse_counts = _min_pulse_counts + (_pulse_span_counts * (uint64_t) duty)/_pwm->period();
        _pwm->setDuty(_min_pulse_counts + (_pulse_span_counts * (uint64_t)duty) / _pwm->period());
        // _pwm->setDuty(_min_pulse_counts+duty); // More efficient by keeping math within 32bits??
        // log_info(name() << " duty:" << duty << " _min_pulse_counts:" << _min_pulse_counts
        //                 << " _pulse_span_counts:" << _pulse_span_counts << " pulse_counts" << pulse_counts);
    }

    // prints the startup message of the spindle config
    void BESC::config_message() {
        log_info(name() << " Spindle Out:" << _output_pin.name() << " Min:" << _min_pulse_us << "us Max:" << _max_pulse_us
                        << "us Freq:" << _pwm->frequency() << "Hz Full Period count:" << _pwm->period());
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<BESC> registration("BESC");
    }
}
