// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is for stepper motors that just require step and direction pins.
*/

#include "StandardStepper.h"

#include "../Machine/MachineConfig.h"
#include "../Stepper.h"   // ST_I2S_*
#include "../Stepping.h"  // Stepping::_engine

#include <esp32-hal-gpio.h>  // gpio
#include <sdkconfig.h>       // CONFIG_IDF_TARGET_*

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
        Assert(_step_pin.defined(), "Step pin must be configured.");
        bool isI2SO = Stepping::_engine == Stepping::I2S_STREAM || Stepping::_engine == Stepping::I2S_STATIC;
        if (isI2SO) {
            Assert(_step_pin.name().rfind("I2SO", 0) == 0, "Step pin must be an I2SO pin");
            if (_dir_pin.defined()) {
                Assert(_dir_pin.name().rfind("I2SO", 0) == 0, "Direction pin must be an I2SO pin");
            }

        } else {
            Assert(_step_pin.name().rfind("gpio", 0) == 0, "Step pin must be a GPIO pin");
            if (_dir_pin.defined()) {
                Assert(_dir_pin.name().rfind("gpio", 0) == 0, "Direction pin must be a GPIO pin");
            }
        }
    }
}
