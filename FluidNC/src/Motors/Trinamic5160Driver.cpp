// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Trinamic5160Driver.h"

#include "../Machine/MachineConfig.h"

#include <atomic>

namespace MotorDrivers {

    void Trinamic5160Driver::init() {
        _has_errors = false;

        uint8_t cs_id;
        cs_id = SPI_setup();

        tmc5160 = new TMC5160Stepper(cs_id, _r_sense, _spi_index);  // TODO hardwired to non daisy chain index

        // use slower speed if I2S
        if (_cs_pin.capabilities().has(Pin::Capabilities::I2S)) {
            tmc5160->setSPISpeed(_spi_freq);
        }

        link = List;
        List = this;

        // Display the stepper library version message once, before the first
        // TMC config message.  Link is NULL for the first TMC instance.
        if (!link) {
            log_debug("TMCStepper Library Ver. 0x" << String(TMCSTEPPER_VERSION, HEX));
        }

        config_message();

        // After initializing all of the TMC drivers, create a task to
        // display StallGuard data.  List == this for the final instance.
        if (List == this) {
            xTaskCreatePinnedToCore(readSgTask,    // task
                                    "readSgTask",  // name for task
                                    4096,          // size of task stack
                                    this,          // parameters
                                    1,             // priority
                                    NULL,
                                    SUPPORT_TASK_CORE  // must run the task on same core
            );
        }
    }

    void Trinamic5160Driver::config_motor() {
        tmc5160->begin();
        _has_errors = !test();  // Try communicating with motor. Prints an error if there is a problem.

        init_step_dir_pins();
        read_settings();  // pull info from settings
        set_mode(false);
    }

    bool Trinamic5160Driver::test() {
        if (_has_errors) {
            return false;
        }

        uint8_t result = tmc5160->test_connection();
        switch (result) {
            case 1:
                log_error(axisName() << " driver test failed. Check connection");
                return false;
            case 2:
                log_error(axisName() << " driver test failed. Check motor power");
                return false;
            default:
                // driver responded, so check for other errors from the DRV_STATUS register

                // TMC2130_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
                // status.sr = tmc2130 ? tmc2130stepper->DRV_STATUS() : tmc5160stepper->DRV_STATUS();;

                // bool err = false;

                // look for errors
                // if (report_short_to_ground(status.s2ga, status.s2gb)) {
                //     err = true;
                // }

                // if (report_over_temp(status.ot, status.otpw)) {
                //     err = true;
                // }

                // if (report_short_to_ps(bits_are_true(status.sr, 12), bits_are_true(status.sr, 13))) {
                //     err = true;
                // }

                // XXX why not report_open_load(status.ola, status.olb) ?

                // if (err) {
                //     return false;
                // }

                log_info(axisName() << " driver test passed");
                return true;
        }
    }

    /*
      Run and hold current configuration items are in (float) Amps,
      but the TMCStepper library expresses run current as (uint16_t) mA
      and hold current as (float) fraction of run current.
    */
    void Trinamic5160Driver::read_settings() {
        if (_has_errors) {
            return;
        }

        uint16_t run_i_ma = (uint16_t)(_run_current * 1000.0);
        float    hold_i_percent;

        if (_run_current == 0) {
            hold_i_percent = 0;
        } else {
            hold_i_percent = _hold_current / _run_current;
            if (hold_i_percent > 1.0) {
                hold_i_percent = 1.0;
            }
        }

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        int usteps = _microsteps == 1 ? 0 : _microsteps;

        tmc5160->microsteps(usteps);
        tmc5160->rms_current(run_i_ma, hold_i_percent);
    }

    void Trinamic5160Driver::set_mode(bool isHoming) {
        if (_has_errors) {
            return;
        }

        _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        tmc5160->tpfd(_tpfd);

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                log_debug("StealthChop");
                tmc5160->en_pwm_mode(true);
                tmc5160->pwm_autoscale(true);
                tmc5160->diag1_stall(false);
                break;
            case TrinamicMode ::CoolStep:
                log_debug("Coolstep");
                tmc5160->en_pwm_mode(false);
                tmc5160->pwm_autoscale(false);
                tmc5160->TCOOLTHRS(NORMAL_TCOOLTHRS);  // when to turn on coolstep
                tmc5160->THIGH(NORMAL_THIGH);
                break;
            case TrinamicMode ::StallGuard:
                log_debug("Stallguard");
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
    }

    // Report diagnostic and tuning info
    void Trinamic5160Driver::debug_message() {
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

        // The bit locations differ somewhat between different chips.
        // The layout is the same for TMC2130 and TMC5160
        // TMC2130_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
        // status.sr = tmc2130 ? tmc2130->DRV_STATUS() : tmc5160->DRV_STATUS();

        // these only report if there is a fault condition
        // report_open_load(status.ola, status.olb);
        // report_short_to_ground(status.s2ga, status.s2gb);
        // report_over_temp(status.ot, status.otpw);
        // report_short_to_ps(bits_are_true(status.sr, 12), bits_are_true(status.sr, 13));

        // log_info(axisName() << " Status Register " << String(status.sr, HEX) << " GSTAT " << String(tmc2130 ? tmc2130->GSTAT() : tmc5160->GSTAT(), HEX));
    }

    void IRAM_ATTR Trinamic5160Driver::set_disable(bool disable) {
        if (_has_errors) {
            return;
        }

        if ((_disabled == disable) && _disable_state_known) {
            return;
        }

        _disable_state_known = true;
        _disabled            = disable;

        _disable_pin.synchronousWrite(_disabled);

        if (_use_enable) {
            uint8_t toff_value;
            if (_disabled) {
                toff_value = _toff_disable;
            } else {
                if (_mode == TrinamicMode::StealthChop) {
                    toff_value = _toff_stealthchop;
                } else {
                    toff_value = _toff_coolstep;
                }
            }

            tmc5160->toff(toff_value);
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<Trinamic5160Driver> registration("tmc_5160");
    }
}
