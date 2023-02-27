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

        TrinamicUartDriver::finalInit();
    }

    void TMC2209Driver::config_motor() {
        tmc2209->begin();
        TrinamicBase::config_motor();
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
    }

    void TMC2209Driver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmc2209->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        if (tmc2209) {
            log_info(axisName() << " SG_Val: " << tmc2209->SG_RESULT() << "   Rate: " << feedrate << " mm/min SG_Setting:" << _stallguard);
        }
    }

    void TMC2209Driver::set_disable(bool disable) {
        if (TrinamicUartDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc2209->toff(TrinamicUartDriver::toffValue());
            }
        }
    }

    bool TMC2209Driver::test() {
        if (!checkVersion(0x21, tmc2209->version())) {
            return false;
        }
        uint8_t ifcnt_before = tmc2209->IFCNT();
        tmc2209->GSTAT(0);  // clear GSTAT to increase ifcnt
        uint8_t ifcnt_after = tmc2209->IFCNT();
        bool    okay        = ((ifcnt_before + 1) & 0xff) == ifcnt_after;
        if (!okay) {
            TrinamicBase::reportCommsFailure();
            return false;
        }
        return true;
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2209Driver> registration("tmc_2209");
    }
}
