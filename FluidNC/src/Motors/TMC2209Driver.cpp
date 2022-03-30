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
        if (!_uart_started) {
            _uart->begin();
            _uart->config_message("Trinamic", " Stepper ");
            _uart_started = true;
        }

        if (_r_sense == 0) {
            _r_sense = TMC2209_RSENSE_DEFAULT;
        }

        tmc2209 = new TMC2209Stepper(_uart, _r_sense, _addr);

        finalInit();
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

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                //log_info("StealthChop");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(true);
                break;
            case TrinamicMode ::CoolStep:
                //log_info("Coolstep");
                // tmc2209->en_pwm_mode(false); //TODO: check if this is present in TMC2208/09
                tmc2209->en_spreadCycle(true);
                tmc2209->pwm_autoscale(false);
                break;
            case TrinamicMode ::StallGuard:  //TODO: check all configurations for stallguard
            {
                auto axisConfig     = config->_axes->_axis[this->axis_index()];
                auto homingFeedRate = (axisConfig->_homing != nullptr) ? axisConfig->_homing->_feedRate : 200;
                //log_info("Stallguard");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(false);
                tmc2209->TCOOLTHRS(calc_tstep(homingFeedRate, 150.0));
                tmc2209->SGTHRS(constrain(_stallguard, 0, 255));
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

        // TMC2208 does not have StallGuard
        if (tmc2209) {
            log_info(axisName() << " SG_Val: " << tmc2209->SG_RESULT() << "   Rate: " << feedrate
                                << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));
        }
    }

    void TMC2209Driver::set_disable(bool disable) {
        if (TrinamicUartDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc2209->toff(TrinamicUartDriver::toffValue());
            }
        }
    }

    bool TMC2209Driver::test() { return TrinamicBase::reportTest(tmc2209->test_connection()); }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2209Driver> registration("tmc_2209");
    }
}
