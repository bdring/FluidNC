// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic SPI controlled stepper motor drivers.
*/

#include "TMC2209Driver.h"
#include "../Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {

    void TMC2209Driver::init() {
        TrinamicUartDriver::init();
        if (!_uart) {
            return;
        }

        if (_r_sense == 0) {
            _r_sense = TMC2209_RSENSE_DEFAULT;
        }

        tmc2209 = new TMC2209Stepper(_uart, _r_sense, _addr);

        registration();
    }

    void TMC2209Driver::config_motor() {
        _cs_pin.synchronousWrite(true);
        tmc2209->begin();
        TrinamicBase::config_motor();
        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        TrinamicMode _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        // Run and hold current configuration items are in (float) Amps,
        // but the TMCStepper library expresses run current as (uint16_t) mA
        // and hold current as (float) fraction of run current.
        uint16_t run_i = (uint16_t)(_run_current * 1000.0);

        _cs_pin.synchronousWrite(true);

        tmc2209->I_scale_analog(false);  // do not scale via pot
        tmc2209->rms_current(run_i, TrinamicBase::holdPercent());

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        int usteps = _microsteps == 1 ? 0 : _microsteps;
        tmc2209->microsteps(usteps);
        tmc2209->pdn_disable(true);  // powerdown pin is disabled. uses ihold.

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                log_debug(axisName() << " StealthChop");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(true);
                break;
            case TrinamicMode ::CoolStep:
                log_debug(axisName() << " Coolstep");
                tmc2209->en_spreadCycle(true);
                tmc2209->pwm_autoscale(false);
                break;
            case TrinamicMode ::StallGuard:  //TODO: check all configurations for stallguard
            {
                auto axisConfig     = config->_axes->_axis[this->axis_index()];
                auto homingFeedRate = (axisConfig->_homing != nullptr) ? axisConfig->_homing->_feedRate : 200;
                log_debug(axisName() << " Stallguard");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(true);
                tmc2209->TCOOLTHRS(calc_tstep(homingFeedRate, 150.0));
                tmc2209->SGTHRS(_stallguard);
                break;
            }
        }

        // dump the registers. This is helpful for people migrating to the Pro version
        log_verbose("CHOPCONF: " << to_hex(tmc2209->CHOPCONF()));
        log_verbose("COOLCONF: " << to_hex(tmc2209->COOLCONF()));
        log_verbose("TPWMTHRS: " << to_hex(tmc2209->TPWMTHRS()));
        log_verbose("TCOOLTHRS: " << to_hex(tmc2209->TCOOLTHRS()));
        log_verbose("GCONF: " << to_hex(tmc2209->GCONF()));
        log_verbose("PWMCONF: " << to_hex(tmc2209->PWMCONF()));
        log_verbose("IHOLD_IRUN: " << to_hex(tmc2209->IHOLD_IRUN()));

        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::debug_message() {
        if (_has_errors) {
            return;
        }

        _cs_pin.synchronousWrite(true);

        uint32_t tstep = tmc2209->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            _cs_pin.synchronousWrite(false);
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        if (tmc2209) {
            log_info(axisName() << " SG_Val: " << tmc2209->SG_RESULT() << "   Rate: " << feedrate << " mm/min SG_Setting:" << _stallguard);
        }

        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::set_disable(bool disable) {
        _cs_pin.synchronousWrite(true);
        if (TrinamicUartDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc2209->toff(TrinamicUartDriver::toffValue());
            }
        }
        _cs_pin.synchronousWrite(false);
    }

    bool TMC2209Driver::test() {
        _cs_pin.synchronousWrite(true);
        if (!checkVersion(0x21, tmc2209->version())) {
            _cs_pin.synchronousWrite(false);
            return false;
        }

        uint8_t ifcnt_before = tmc2209->IFCNT();
        tmc2209->GSTAT(0);  // clear GSTAT to increase ifcnt
        uint8_t ifcnt_after = tmc2209->IFCNT();
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
        MotorFactory::InstanceBuilder<TMC2209Driver> registration("tmc_2209");
    }
}
