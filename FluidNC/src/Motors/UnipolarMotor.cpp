// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    Unipolar stepper motors, such as the 28BYJ-48 driven by a ULN2003 board.

    These have no step/direction inputs; the four coil phases are driven
    directly, so this driver energizes the phase pins itself rather than
    handing a step pin to the stepping engine.  It registers with
    Stepping::assignMotorDriver(), which then calls set_direction() and step()
    from the step ISR in place of the engine's pin operations.
*/

#include "UnipolarMotor.h"

#include "Machine/MachineConfig.h"
#include "Stepping.h"     // Stepping::assignMotorDriver()
#include "string_util.h"  // starts_with_ignore_case()

using namespace Machine;

namespace MotorDrivers {
    void UnipolarMotor::init() {
        _pin_phase0.setAttr(Pin::Attr::Output);
        _pin_phase1.setAttr(Pin::Attr::Output);
        _pin_phase2.setAttr(Pin::Attr::Output);
        _pin_phase3.setAttr(Pin::Attr::Output);
        _current_phase = 0;
        config_message();

        Stepping::assignMotorDriver(axis_index(), dual_axis_index(), this);
    }

    void UnipolarMotor::validate() {
        Assert(!_pin_phase0.undefined(), "Phase 0 pin should be configured.");
        Assert(!_pin_phase1.undefined(), "Phase 1 pin should be configured.");
        Assert(!_pin_phase2.undefined(), "Phase 2 pin should be configured.");
        Assert(!_pin_phase3.undefined(), "Phase 3 pin should be configured.");

        // The phase pins are written directly from the step ISR, bypassing the
        // stepping engine, so they must be pins that can be set immediately.
        for (auto pin : { &_pin_phase0, &_pin_phase1, &_pin_phase2, &_pin_phase3 }) {
            Assert(string_util::starts_with_ignore_case(pin->name(), "gpio"), "Phase pin %s type must be gpio", pin->name());
        }
    }

    void UnipolarMotor::config_message() {
        log_info("    " << name() << " Ph0:" << _pin_phase0.name() << " Ph1:" << _pin_phase1.name() << " Ph2:" << _pin_phase2.name()
                        << " Ph3:" << _pin_phase3.name() << " Half Step:" << (_half_step ? "Yes" : "No"));
    }

    void IRAM_ATTR UnipolarMotor::set_disable(bool disable) {
        if (disable) {
            _pin_phase0.off();
            _pin_phase1.off();
            _pin_phase2.off();
            _pin_phase3.off();
        }
        _enabled = !disable;
    }

    void IRAM_ATTR UnipolarMotor::set_direction(bool dir) {
        _dir = dir;
    }

    void IRAM_ATTR UnipolarMotor::step() {
        if (!_enabled) {
            return;  // don't do anything, phase is not changed or lost
        }

        /*
			8 Step : A – AB – B – BC – C – CD – D – DA
			4 Step : AB – BC – CD – DA

			Step		IN4	IN3	IN2	IN1
			A 		0 	0 	0 	1
			AB		0	0	1	1
			B		0	0	1	0
			BC		0	1	1	0
			C		0	1	0	0
			CD		1	1	0	0
			D		1	0	0	0
			DA		1	0	0	1

            Those sequences are packed one nibble per step, least significant
            nibble first, so that the pattern for a step is a shift and mask of
            a compile-time constant.  A lookup table would live in flash, which
            is not safe to touch from an ISR.
        */
        const uint32_t sequence  = _half_step ? 0x98c46231 : 0x00009c63;
        const uint8_t  phase_max = _half_step ? 7 : 3;

        if (_dir) {  // count up
            _current_phase = _current_phase == phase_max ? 0 : _current_phase + 1;
        } else {  // count down
            _current_phase = _current_phase == 0 ? phase_max : _current_phase - 1;
        }

        const uint32_t pattern = sequence >> (_current_phase * 4);

        _pin_phase0.write((pattern & 1) != 0);
        _pin_phase1.write((pattern & 2) != 0);
        _pin_phase2.write((pattern & 4) != 0);
        _pin_phase3.write((pattern & 8) != 0);
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<UnipolarMotor> registration("unipolar");
    }
}
