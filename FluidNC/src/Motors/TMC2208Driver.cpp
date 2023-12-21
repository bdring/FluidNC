// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic SPI controlled stepper motor drivers.
*/

#include "TMC2208Driver.h"
#include "../Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {

    void TMC2208Driver::init() {
        TrinamicUartDriver::init();
        if (!_uart) {
            return;
        }
        if (_r_sense == 0) {
            _r_sense = TMC2208_RSENSE_DEFAULT;
        }

        tmc2208 = new TMC2209Stepper(_uart, _r_sense, _addr);

        registration();
    }

    void TMC2208Driver::config_motor() {
        _cs_pin.synchronousWrite(true);
        tmc2208->begin();
        TrinamicBase::config_motor();
        _cs_pin.synchronousWrite(false);
    }

    void TMC2208Driver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        TrinamicMode _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        // Run and hold current configuration items are in (float) Amps,
        // but the TMCStepper library expresses run current as (uint16_t) mA
        // and hold current as (float) fraction of run current.
        uint16_t run_i = (uint16_t)(_run_current * 1000.0);

        _cs_pin.synchronousWrite(true);

        tmc2208->I_scale_analog(false);  // do not scale via pot
        tmc2208->rms_current(run_i, TrinamicBase::holdPercent());

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        int usteps = _microsteps == 1 ? 0 : _microsteps;
        tmc2208->microsteps(usteps);

        // This driver does not support multiple modes
        tmc2208->en_spreadCycle(false);
        tmc2208->pwm_autoscale(true);

        _cs_pin.synchronousWrite(false);
    }

    void TMC2208Driver::debug_message() {}

    void TMC2208Driver::set_disable(bool disable) {
        _cs_pin.synchronousWrite(true);
        if (TrinamicUartDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc2208->toff(TrinamicUartDriver::toffValue());
            }
        }
        _cs_pin.synchronousWrite(false);
    }

    bool TMC2208Driver::test() {
        _cs_pin.synchronousWrite(true);
        if (!checkVersion(0x20, tmc2208->version())) {
            _cs_pin.synchronousWrite(false);
            return false;
        }
        uint8_t ifcnt_before = tmc2208->IFCNT();
        tmc2208->GSTAT(0);  // clear GSTAT to increase ifcnt
        uint8_t ifcnt_after = tmc2208->IFCNT();
        bool    okay        = ((ifcnt_before + 1) & 0xff) == ifcnt_after;
        if (!okay) {
            TrinamicBase::reportCommsFailure();
            _cs_pin.synchronousWrite(false);
            return false;
        }
        _cs_pin.synchronousWrite(false);
        return true;
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2208Driver> registration("tmc_2208");
    }
}
