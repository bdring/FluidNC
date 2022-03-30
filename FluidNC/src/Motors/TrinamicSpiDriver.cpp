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

    pinnum_t TrinamicSpiDriver::daisy_chain_cs_id = 255;
    uint8_t  TrinamicSpiDriver::spi_index_mask    = 0;

    void TrinamicSpiDriver::init() {} 
    
    uint8_t TrinamicSpiDriver::setupSPI() {
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

        return cs_id;
    }

    void TrinamicSpiDriver::finalInit() {
        _has_errors = false;

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

    /*
    This is the startup message showing the basic definition
    */
    void TrinamicSpiDriver::config_message() {
        log_info("    " << name() << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " CS:" << _cs_pin.name()
                        << " Disable:" << _disable_pin.name() << " Index:" << _spi_index << " R:" << _r_sense);
    }

    void TrinamicSpiDriver::set_registers(bool isHoming) {}

    bool TrinamicSpiDriver::set_homing_mode(bool isHoming) {
        set_registers(isHoming);
        return true;
    }

    float TrinamicSpiDriver::holdPercent() {
        if (_run_current == 0) {
            return 0.0;
        }

        float hold_percent = _hold_current / _run_current;
        if (hold_percent > 1.0) {
            hold_percent = 1.0;
        }

        return hold_percent;
    }

    bool TrinamicSpiDriver::reportTest(uint8_t result) {
        if (_has_errors) {
            return false;
        }

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

    uint8_t TrinamicSpiDriver::toffValue() {
        if (_disabled) {
            return _toff_disable;
        }
        return _mode == TrinamicMode::StealthChop ? _toff_stealthchop : _toff_coolstep;
    }

    bool TrinamicSpiDriver::startDisable(bool disable) {
        if (_has_errors) {
            return false;
        }

        if ((_disabled == disable) && _disable_state_known) {
            return false;
        }

        _disable_state_known = true;
        _disabled            = disable;

        _disable_pin.synchronousWrite(_disabled);
        return true;
    }
}
