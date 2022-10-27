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

    uint8_t TrinamicSpiDriver::toffValue() {
        if (_disabled) {
            return _toff_disable;
        }
        return _mode == TrinamicMode::StealthChop ? _toff_stealthchop : _toff_coolstep;
    }

}
