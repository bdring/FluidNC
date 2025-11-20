// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for stepper motors that just require step and direction pins.
*/

#include "StandardStepper.h"

#include "Machine/MachineConfig.h"
#include "Stepper.h"      // ST_I2S_*
#include "Stepping.h"     // Stepping::_engine
#include "string_util.h"  // starts_with_ignore_case()

using namespace Machine;

namespace MotorDrivers {

    void StandardStepper::init() {
        config_message();
        init_step_dir_pins();
    }

    void StandardStepper::init_step_dir_pins() {
        auto axisIndex     = axis_index();
        auto dualAxisIndex = dual_axis_index();

        _step_pin.setAttr(Pin::Attr::Output);
        _dir_pin.setAttr(Pin::Attr::Output);

        if (_disable_pin.defined()) {
            _disable_pin.setAttr(Pin::Attr::Output);
        }

        if (_step_pin.canStep()) {
            Stepping::assignMotor(axisIndex, dualAxisIndex, _step_pin.index(), _step_pin.inverted(), _dir_pin.index(), _dir_pin.inverted());
        }
    }

    void StandardStepper::config_message() {
        log_info("    " << name() << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " Disable:" << _disable_pin.name());
    }

    void IRAM_ATTR StandardStepper::set_disable(bool disable) {
        _disable_pin.synchronousWrite(disable);
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<StandardStepper> registration("standard_stepper");
    }

    void StandardStepper::validate() {
        Assert(_step_pin.defined(), "Step pin must be configured");
        bool        isI2SO = Stepping::_engine == Stepping::I2S_STREAM || Stepping::_engine == Stepping::I2S_STATIC;
        const char* type   = isI2SO ? "i2so" : "gpio";
        Assert(string_util::starts_with_ignore_case(_step_pin.name(), type), "Step pin %s type must be %s", _step_pin.name(), type);
        if (_dir_pin.defined()) {
            Assert(string_util::starts_with_ignore_case(_dir_pin.name(), type), "Direction pin %s type must be %s", _dir_pin.name(), type);
        }
    }
}
