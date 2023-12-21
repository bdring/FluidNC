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
        Assert(_uart, "TMC Driver missing uart%d section", _uart_num);

        _cs_pin.setAttr(Pin::Attr::Output);
    }

    /*
        This is the startup message showing the basic definition. 

        log_info(" UART CS:" << );
    */
    void TrinamicUartDriver::config_message() {  //TODO: The RX/TX pin could be added to the msg.
        log_info("    " << name() << " UART" << _uart_num << " Addr:" << _addr << " CS:" << _cs_pin.name() << " Step:" << _step_pin.name()
                        << " Dir:" << _dir_pin.name() << " Disable:" << _disable_pin.name() << " R:" << _r_sense);
    }

    uint8_t TrinamicUartDriver::toffValue() {
        if (_disabled) {
            return _toff_disable;
        }
        return _mode == TrinamicMode::StealthChop ? _toff_stealthchop : _toff_coolstep;
    }

}
