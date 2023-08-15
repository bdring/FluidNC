// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TMC5160Driver.h"
#include "../Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {

    void TMC5160Driver::init() {
        uint8_t cs_id;
        cs_id = setupSPI();

        if (_r_sense == 0) {
            _r_sense = TMC5160_RSENSE_DEFAULT;
        }

        tmc5160 = new TMC5160Stepper(cs_id, _r_sense, _spi_index);

        // use slower speed if I2S
        if (_cs_pin.capabilities().has(Pin::Capabilities::I2S)) {
            tmc5160->setSPISpeed(_spi_freq);
        }
        registration();
    }

    void TMC5160Driver::config_motor() {
        tmc5160->begin();
        TrinamicBase::config_motor();
    }

    bool TMC5160Driver::test() { return checkVersion(0x30, tmc5160->version()); }

    void TMC5160Driver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        // Run and hold current configuration items are in (float) Amps,
        // but the TMCStepper library expresses run current as (uint16_t) mA
        // and hold current as (float) fraction of run current.
        uint16_t run_i = (uint16_t)(_run_current * 1000.0);
        tmc5160->rms_current(run_i, TrinamicSpiDriver::holdPercent());

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        int usteps = _microsteps == 1 ? 0 : _microsteps;
        tmc5160->microsteps(usteps);

        tmc5160->tpfd(_tpfd);

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                log_debug(axisName() << " StealthChop");
                tmc5160->en_pwm_mode(true);
                tmc5160->pwm_autoscale(true);
                tmc5160->diag1_stall(false);
                break;
            case TrinamicMode ::CoolStep:
                log_debug(axisName() << " Coolstep");
                tmc5160->en_pwm_mode(false);
                tmc5160->pwm_autoscale(false);
                tmc5160->TCOOLTHRS(NORMAL_TCOOLTHRS);  // when to turn on coolstep
                tmc5160->THIGH(NORMAL_THIGH);
                break;
            case TrinamicMode ::StallGuard:
                log_debug(axisName() << " Stallguard");
                {
                    auto feedrate = config->_axes->_axis[axis_index()]->_homing->_feedRate;

                    tmc5160->en_pwm_mode(false);
                    tmc5160->pwm_autoscale(false);
                    tmc5160->TCOOLTHRS(calc_tstep(feedrate, 150.0));
                    tmc5160->THIGH(calc_tstep(feedrate, 60.0));
                    tmc5160->sfilt(1);
                    tmc5160->diag1_stall(true);  // stallguard i/o is on diag1
                    tmc5160->sgt(constrain(_stallguard, -64, 63));
                    break;
                }
        }
        // dump the registers. This is helpful for people migrating to the Pro version
        log_verbose("CHOPCONF: " << to_hex(tmc5160->CHOPCONF()));
        log_verbose("COOLCONF: " << to_hex(tmc5160->COOLCONF()));
        log_verbose("THIGH: " << to_hex(tmc5160->THIGH()));
        log_verbose("TCOOLTHRS: " << to_hex(tmc5160->TCOOLTHRS()));
        log_verbose("GCONF: " << to_hex(tmc5160->GCONF()));
        log_verbose("PWMCONF: " << to_hex(tmc5160->PWMCONF()));
        log_verbose("IHOLD_IRUN: " << to_hex(tmc5160->IHOLD_IRUN()));
    }

    // Report diagnostic and tuning info
    void TMC5160Driver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmc5160->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        log_info(axisName() << " Stallguard " << tmc5160->stallguard() << "   SG_Val:" << tmc5160->sg_result() << " Rate:" << feedrate
                            << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));
    }

    void TMC5160Driver::set_disable(bool disable) {
        if (TrinamicSpiDriver::startDisable(disable)) {
            if (_use_enable) {
                tmc5160->toff(TrinamicSpiDriver::toffValue());
            }
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC5160Driver> registration("tmc_5160");
    }
}
