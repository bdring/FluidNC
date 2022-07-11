// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is similar to the PWM Spindle except that it enables the
    M4 speed vs. power compensation.
*/

#include "Laser.h"
#include "Driver/PwmPin.h"  // pwmInit(), etc.

#include "../Machine/MachineConfig.h"

// ===================================== Laser ==============================================

namespace Spindles {
    bool Laser::isRateAdjusted() {
        return true;  // can use M4 (CCW) laser mode.
    }

    void Laser::config_message() {
        log_info(name() << " Spindle Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Freq:" << _pwm->frequency()
                        << "Hz Period:" << _pwm->period() << " Laser mode:On");
    }

    // Get the GPIO from the machine definition
    void Laser::get_pins_and_settings() {
        // setup all the pins
        PWM::get_pins_and_settings();

        is_reversable = false;

        if (_speeds.size() == 0) {
            // The default speed map for a Laser is linear from 0=0% to 255=100%
            linearSpeeds(255, 100.0f);
        }

        setupSpeeds(_pwm->period());
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Laser> registration("Laser");
    }
}
