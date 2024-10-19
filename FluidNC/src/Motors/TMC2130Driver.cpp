// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic SPI controlled stepper motor drivers.
*/

#include "TMC2130Driver.h"
#include "../Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {

    void TMC2130Driver::init() {
        TrinamicSpiDriver::init();

        uint8_t cs_id;
        cs_id = setupSPI();

        if (_r_sense == 0) {
            _r_sense = TMC2130_RSENSE_DEFAULT;
        }

        tmc2130 = new TMC2130Stepper(cs_id, _r_sense, _spi_index);  // TODO hardwired to non daisy chain index

        registration();
    }

    void TMC2130Driver::config_motor() {
        tmc2130->begin();
        TrinamicBase::config_motor();
    }

    bool TMC2130Driver::test() {
        return checkVersion(0x11, tmc2130->version());
    }

    void TMC2130Driver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        // Run and hold current configuration items are in (float) Amps,
        // but the TMCStepper library expresses run current as (uint16_t) mA
        // and hold current as (float) fraction of run current.
        uint16_t run_i = (uint16_t)(_run_current * 1000.0);
        tmc2130->I_scale_analog(false);  // do not scale via pot
        tmc2130->rms_current(run_i, TrinamicSpiDriver::holdPercent());

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        int usteps = _microsteps == 1 ? 0 : _microsteps;
        tmc2130->microsteps(usteps);

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                log_debug(axisName() << " StealthChop");
                tmc2130->en_pwm_mode(true);
                tmc2130->pwm_autoscale(true);
                tmc2130->diag1_stall(false);
                break;
            case TrinamicMode ::CoolStep:
                log_debug(axisName() << " Coolstep");
                tmc2130->en_pwm_mode(false);
                tmc2130->pwm_autoscale(false);
                tmc2130->TCOOLTHRS(NORMAL_TCOOLTHRS);  // when to turn on coolstep
                tmc2130->THIGH(NORMAL_THIGH);
                break;
            case TrinamicMode ::StallGuard:
                log_debug(axisName() << " Stallguard");
                {
                    tmc2130->en_pwm_mode(false);
                    tmc2130->pwm_autoscale(false);
                    tmc2130->TCOOLTHRS(calc_tstep(150));
                    tmc2130->THIGH(calc_tstep(60));
                    tmc2130->sfilt(1);
                    tmc2130->diag1_stall(true);  // stallguard i/o is on diag1
                    tmc2130->sgt(constrain(_stallguard, -64, 63));
                }
                break;
        }
    }

    // Report diagnostic and tuning info
    void TMC2130Driver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmc2130->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        log_info(axisName() << " Stallguard " << tmc2130->stallguard() << "   SG_Val:" << tmc2130->sg_result() << " Rate:" << feedrate
                            << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));
    }

    void TMC2130Driver::set_disable(bool disable) {
        if (TrinamicSpiDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc2130->toff(TrinamicSpiDriver::toffValue());
            }
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2130Driver> registration("tmc_2130");
    }
}
