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

    Uart* TrinamicUartDriver::_uart = nullptr;

    bool TrinamicUartDriver::_uart_started = false;

    /* HW Serial Constructor. */
    TrinamicUartDriver::TrinamicUartDriver(uint16_t driver_part_number, uint8_t addr) : TrinamicBase(driver_part_number), _addr(addr) {}

    void TrinamicUartDriver::init() {
        if (!_uart_started) {
            _uart->begin();
            _uart->config_message("Trinamic", " Stepper ");
            _uart_started = true;
        }
        _has_errors = hw_serial_init();

        link = List;
        List = this;

        if (_has_errors) {
            log_warn("TMCStepper UART init has errors");
            return;
        }

        // Display the stepper library version message once, before the first
        // TMC config message.  Link is NULL for the first TMC instance.
        if (!link) {
            log_debug("TMCStepper Library Ver. 0x" << String(TMCSTEPPER_VERSION, HEX));
        }

        config_message();

        if (tmc2208) {
            tmc2208->begin();
        } else {
            tmc2209->begin();
        }
        _has_errors = !test();  // Try communicating with motor. Prints an error if there is a problem.

        init_step_dir_pins();
        read_settings();  // pull info from settings
        set_mode(false);

        // After initializing all of the TMC drivers, create a task to
        // display StallGuard data.  List == this for the final instance.
        if (List == this) {
            xTaskCreatePinnedToCore(readSgTask,    // task
                                    "readSgTask",  // name for task
                                    4096,          // size of task stack
                                    NULL,          // parameters
                                    1,             // priority
                                    NULL,
                                    SUPPORT_TASK_CORE  // must run the task on same core
            );
        }
    }

    bool TrinamicUartDriver::hw_serial_init() {
        if (_driver_part_number == 2208) {
            tmc2208 = new TMC2208Stepper(_uart, _r_sense);
            return false;
        }
        if (_driver_part_number == 2209) {
            tmc2209 = new TMC2209Stepper(_uart, _r_sense, _addr);
            return false;
        }
        log_error("Unsupported Trinamic motor p/n:" << _driver_part_number);
        return true;
    }

    /*
        This is the startup message showing the basic definition. 
    */
    void TrinamicUartDriver::config_message() {  //TODO: The RX/TX pin could be added to the msg.
        log_info("    " << name() << " Step:" << _step_pin.name() << " Dir:" << _dir_pin.name() << " Disable:" << _disable_pin.name()
                        << " Addr:" << _addr << " R:" << _r_sense);
    }

    bool TrinamicUartDriver::test() {
        if (_has_errors) {
            return false;
        }

        uint8_t result = tmc2208 ? tmc2208->test_connection() : tmc2209->test_connection();
        switch (result) {
            case 1:
                log_error("    " << axisName() << " Trinamic driver test failed. Check connection");
                return false;
            case 2:
                log_error("    " << axisName() << " Trinamic driver test failed. Check motor power");
                return false;
            default:
                // driver responded, so check for other errors from the DRV_STATUS register

                TMC2208_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
                status.sr = tmc2208 ? tmc2208->DRV_STATUS() : tmc2209->DRV_STATUS();

                bool err = false;

                // look for errors
                if (report_short_to_ground(status.s2ga, status.s2gb)) {
                    err = true;
                }

                if (report_over_temp(status.ot, status.otpw)) {
                    err = true;
                }

                if (report_short_to_ps(bits_are_true(status.sr, 12), bits_are_true(status.sr, 13))) {
                    err = true;
                }

                // XXX why not report_open_load(status.ola, status.olb) ?

                if (err) {
                    return false;
                }

                log_info("    driver test passed");
                return true;
        }
    }

    /*
      Run and hold current configuration items are in (float) Amps,
      but the TMCStepper library expresses run current as (uint16_t) mA
      and hold current as (float) fraction of run current.
    */
    void TrinamicUartDriver::read_settings() {
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
        if (tmc2208) {
            tmc2208->microsteps(usteps);
            tmc2208->rms_current(run_i_ma, hold_i_percent);
        } else {
            tmc2209->microsteps(usteps);
            tmc2209->rms_current(run_i_ma, hold_i_percent);
        }
    }

    // XXX Identical to TrinamicDriver::set_homing_mode()
    bool TrinamicUartDriver::set_homing_mode(bool isHoming) {
        set_mode(isHoming);
        return true;
    }

    void TrinamicUartDriver::set_mode(bool isHoming) {
        if (_has_errors) {
            return;
        }

        TrinamicMode _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);
        if (tmc2208) {
            switch (_mode) {
                case TrinamicMode ::StealthChop:
                    //log_info("StealthChop");
                    tmc2208->en_spreadCycle(false);
                    tmc2208->pwm_autoscale(true);
                    break;
                default:
                    log_error("Unsupported TMC2208 mode:" << _mode);
            }
        } else {
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
    }

    //  Report diagnostic and tuning info.
    void TrinamicUartDriver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmc2208 ? tmc2208->TSTEP() : tmc2209->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        // TMC2208 does not have StallGuard
        if (tmc2209) {
            log_info(axisName() << " SG_Val: " << tmc2209->SG_RESULT() << "   Rate: " << feedrate
                                << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));
        }

        // TMC2208_n ::DRV_STATUS_t status { 0 };  // a useful struct to access the bits.
        // status.sr = tmc2208 ? tmc2208->DRV_STATUS() : tmc2209->DRV_STATUS();

        // these only report if there is a fault condition
        // report_open_load(status.ola, status.olb);
        // report_short_to_ground(status.s2ga, status.s2gb);
        // report_over_temp(status.ot, status.otpw);
        // report_short_to_ps(bits_are_true(status.sr, 12), bits_are_true(status.sr, 13));

        // log_info(axisName()<<" Status Register "<<String(status.sr,HEX)<<" GSTAT " << String(tmc2208 ? tmc2208->GSTAT() : tmc2209->GSTAT(),HEX));
    }

    void IRAM_ATTR TrinamicUartDriver::set_disable(bool disable) {
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
            if (tmc2208) {
                tmc2208->toff(toff_value);
            } else {
                tmc2209->toff(toff_value);
            }
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2208> registration_2208("tmc_2208");
        MotorFactory::InstanceBuilder<TMC2209> registration_2209("tmc_2209");
    }
}
