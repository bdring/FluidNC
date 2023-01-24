// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2020 -	The Ant Team
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic UART controlled stepper motor drivers.

    TMC2209 Datasheet
    https://www.trinamic.com/fileadmin/assets/Products/ICs_Documents/TMC2209_Datasheet_V103.pdf
*/

#include "TrinamicUartDriver.h"

#include "../Machine/MachineConfig.h"
#include "../Uart.h"

#include <TMCStepper.h>  // https://github.com/teemuatlut/TMCStepper
#include <atomic>

namespace MotorDrivers {

    void TrinamicUartDriver::init() {
        _uart = config->_uarts[_uart_num];
        if (!_uart) {
            log_error("TMC2208: Missing uart" << _uart_num << "section");
            return;
        }
    }

    /*
        This is the startup message showing the basic definition. 
    */
    void TrinamicUartDriver::config_message() {  //TODO: The RX/TX pin could be added to the msg.
        log_info("    " << name() << " UART" << _uart_num << " Addr:" << _addr << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name()
                        << " Disable:" << _disable_pin.name() << " R:" << _r_sense);
    }

    void TrinamicUartDriver::finalInit() {
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

    uint8_t TrinamicUartDriver::toffValue() {
        if (_disabled) {
            return _toff_disable;
        }
        return _mode == TrinamicMode::StealthChop ? _toff_stealthchop : _toff_coolstep;
    }

}
