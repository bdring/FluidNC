// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic SPI controlled stepper motor drivers.
*/

#include "TrinamicSpiDriver.h"

#include "../Machine/MachineConfig.h"

#include <TMCStepper.h>  // https://github.com/teemuatlut/TMCStepper
#include <atomic>

namespace MotorDrivers {
    TrinamicSpiDriver::TrinamicSpiDriver(uint16_t driver_part_number) : TrinamicBase(driver_part_number) {}

    pinnum_t TrinamicSpiDriver::daisy_chain_cs_id = 255;
    uint8_t  TrinamicSpiDriver::spi_index_mask    = 0;

    void TrinamicSpiDriver::init() {
        _has_errors = false;

        auto spiConfig = config->_spi;
        Assert(spiConfig && spiConfig->defined(), "SPI bus is not configured. Cannot initialize TMC driver.");

        uint8_t cs_id;
        if (daisy_chain_cs_id != 255) {
            cs_id = daisy_chain_cs_id;
        } else {
            _cs_pin.setAttr(Pin::Attr::Output | Pin::Attr::InitialOn);
            _cs_mapping = PinMapper(_cs_pin);
            cs_id       = _cs_mapping.pinId();
        }

        if (_driver_part_number == 2130) {
            //log_info("ID: " << cs_id << " index:" << _spi_index);
            tmcstepper = new TMC2130Stepper(cs_id, _r_sense, _spi_index);  // TODO hardwired to non daisy chain index
        } else if (_driver_part_number == 5160) {
            tmcstepper = new TMC5160Stepper(cs_id, _r_sense, _spi_index);
        } else {
            log_info("    Unsupported Trinamic part number TMC" << _driver_part_number);
            _has_errors = true;  // This motor cannot be used
            return;
        }

        _has_errors = false;

        // use slower speed if I2S
        if (_cs_pin.capabilities().has(Pin::Capabilities::I2S)) {
            tmcstepper->setSPISpeed(_spi_freq);
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

    void TrinamicSpiDriver::config_motor() {
        tmcstepper->begin();

        _has_errors = !test();  // Try communicating with motor. Prints an error if there is a problem.

        init_step_dir_pins();
        read_settings();  // pull info from settings
        set_mode(false);
    }

    /*
    This is the startup message showing the basic definition
    */
    void TrinamicSpiDriver::config_message() {
        log_info("    Trinamic TMC" << _driver_part_number << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " CS:"
                                    << _cs_pin.name() << " Disable:" << _disable_pin.name() << " Index:" << _spi_index << " R:" << _r_sense);
    }

    bool TrinamicSpiDriver::test() {
        if (_has_errors) {
            return false;
        }

        switch (tmcstepper->test_connection()) {
            case 1:
                log_info(axisName() << " Trinamic driver test failed. Check connection");
                return false;
            case 2:
                log_info(axisName() << " Trinamic driver test failed. Check motor power");
                return false;
            default:
                // driver responded, so check for other errors from the DRV_STATUS register

                // TMC2130_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
                // status.sr = tmcstepper->DRV_STATUS();

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

                log_info(axisName() << " Trinamic driver test passed");
                return true;
        }
    }

    /*
    Read setting and send them to the driver. Called at init() and whenever related settings change
    both are stored as float Amps, but TMCStepper library expects...
    uint16_t run (mA)
    float hold (as a percentage of run)
    */
    void TrinamicSpiDriver::read_settings() {
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

        tmcstepper->microsteps(_microsteps);
        tmcstepper->rms_current(run_i_ma, hold_i_percent);
    }

    bool TrinamicSpiDriver::set_homing_mode(bool isHoming) {
        set_mode(isHoming);
        return true;
    }

    /*
    There are ton of settings. I'll start by grouping then into modes for now.
    Many people will want quiet and stallguard homing. Stallguard only run in
    Coolstep mode, so it will need to switch to Coolstep when homing
    */
    void TrinamicSpiDriver::set_mode(bool isHoming) {
        if (_has_errors) {
            return;
        }

        TrinamicMode newMode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        if (newMode == _mode) {
            return;
        }
        _mode = newMode;

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                //log_info("StealthChop");
                tmcstepper->en_pwm_mode(true);
                tmcstepper->pwm_autoscale(true);
                tmcstepper->diag1_stall(false);
                break;
            case TrinamicMode ::CoolStep:
                //log_info("Coolstep");
                tmcstepper->en_pwm_mode(false);
                tmcstepper->pwm_autoscale(false);
                tmcstepper->TCOOLTHRS(NORMAL_TCOOLTHRS);  // when to turn on coolstep
                tmcstepper->THIGH(NORMAL_THIGH);
                break;
            case TrinamicMode ::StallGuard:
                //log_info("Stallguard");
                {
                    auto feedrate = config->_axes->_axis[axis_index()]->_homing->_feedRate;

                    tmcstepper->en_pwm_mode(false);
                    tmcstepper->pwm_autoscale(false);
                    tmcstepper->TCOOLTHRS(calc_tstep(feedrate, 150.0));
                    tmcstepper->THIGH(calc_tstep(feedrate, 60.0));
                    tmcstepper->sfilt(1);
                    tmcstepper->diag1_stall(true);  // stallguard i/o is on diag1
                    tmcstepper->sgt(constrain(_stallguard, -64, 63));
                    break;
                }
            case TrinamicMode ::Unknown:
                log_info("TrinamicMode ::Unknown");
                break;
        }
    }

    /*
    This is the stallguard tuning info. It is call debug, so it could be generic across all classes.
*/
    void TrinamicSpiDriver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmcstepper->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        log_info(axisName() << " Stallguard " << tmcstepper->stallguard() << "   SG_Val:" << tmcstepper->sg_result() << " Rate:" << feedrate
                            << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));

        // The bit locations differ somewhat between different chips.
        // The layout is very different between 2130 and 2208
        TMC2130_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
        status.sr = tmcstepper->DRV_STATUS();

        // these only report if there is a fault condition
        report_open_load(status.ola, status.olb);
        report_short_to_ground(status.s2ga, status.s2gb);
        report_over_temp(status.ot, status.otpw);
        report_short_to_ps(bits_are_true(status.sr, 12), bits_are_true(status.sr, 13));

        // log_info(axisName() << " Status Register " << String(status.sr, HEX) << " GSTAT " << String(tmcstepper->GSTAT(), HEX));
    }

    // this can use the enable feature over SPI. The dedicated pin must be in the enable mode,
    // but that can be hardwired that way.
    void IRAM_ATTR TrinamicSpiDriver::set_disable(bool disable) {
        if (_has_errors) {
            return;
        }

        if (_disabled == disable) {
            return;
        }

        _disabled = disable;

        _disable_pin.synchronousWrite(_disabled);

        if (_use_enable) {
            if (_disabled) {
                tmcstepper->toff(_toff_disable);
            } else {
                if (_mode == TrinamicMode::StealthChop) {
                    tmcstepper->toff(_toff_stealthchop);
                } else {
                    tmcstepper->toff(_toff_coolstep);
                }
            }
        }
        // the pin based enable could be added here.
        // This would be for individual motors, not the single pin for all motors.
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2130> registration_2130("tmc_2130");
        MotorFactory::InstanceBuilder<TMC5160> registration_5160("tmc_5160");
    }
}
