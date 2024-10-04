// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is similar to the PWM Spindle except that it enables the
    M4 speed vs. power compensation.
*/

#include "LaserSpindle.h"
#include "Driver/PwmPin.h"  // pwmInit(), etc.

#include "../Machine/MachineConfig.h"

// ===================================== Laser ==============================================

namespace Spindles {
    bool Laser::isRateAdjusted() {
        return true;  // can use M4 (CCW) laser mode.
    }

    void Laser::config_message() {
        log_info(name() << " Ena:" << _enable_pin.name() << " Out:" << _output_pin.name() << " Freq:" << _pwm->frequency()
                        << "Hz Period:" << _pwm->period() << atc_info());
    }

    void Laser::init() {
        if (_speeds.size() == 0) {
            // The default speed map for a Laser is linear from 0=0% to 255=100%
            linearSpeeds(255, 100.0f);
        }
        // A speed map is now present and PWM::init() will not set its own default

        PWM::init();

        // Turn off is_reversable regardless of what PWM::init() thinks.
        // Laser mode uses M4 for speed-dependent power instead of CCW rotation.
        is_reversable = false;
    }

    // Configuration registration
    namespace {
        SpindleFactory::InstanceBuilder<Laser> registration("Laser");
    }
}
