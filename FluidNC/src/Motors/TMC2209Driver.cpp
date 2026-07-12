// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is used for Trinamic SPI controlled stepper motor drivers.
*/

#include "TMC2209Driver.h"
#include "Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {
    std::vector<TMC2209Driver*> TMC2209Driver::_tmc2209_instances;

    bool TMC2209Driver::sameUartAddress(const TMC2209Driver& other) const {
        TMC2209UartEndpoint endpoint { _uart_num, _addr, !_cs_pin.undefined() };
        TMC2209UartEndpoint other_endpoint { other._uart_num, other._addr, !other._cs_pin.undefined() };
        return endpoint.sharesUnselectedAddress(other_endpoint);
    }

    TMC2209UartSettings TMC2209Driver::uartSettings() const {
        return { _r_sense,       _run_current,      _hold_current,   _homing_current, _microsteps,    _run_mode,
                 _homing_mode,   _stallguard,       _toff_disable,   _toff_stealthchop, _toff_coolstep, _use_enable };
    }

    void TMC2209Driver::validate() {
        TrinamicUartDriver::validate();
        if (tmc2209RequiresReadback(_shared_address_write_only)) {
            return;
        }

        Assert(_cs_pin.undefined(), "shared_address_write_only requires cs_pin: NO_PIN");
        Assert(!_stallguardDebugMode, "shared_address_write_only does not support stallguard_debug");

        for (auto* other : _tmc2209_instances) {
            if (other == this || !sameUartAddress(*other)) {
                continue;
            }
            Assert(other->_shared_address_write_only,
                   "TMC2209 UART%d address %d is shared; all drivers at that address must set shared_address_write_only: true", _uart_num,
                   _addr);
            const char* mismatch = uartSettings().mismatch(other->uartSettings());
            Assert(!mismatch, "TMC2209 UART%d address %d shared-address setting conflict: %s", _uart_num, _addr, mismatch);
        }
    }

    void TMC2209Driver::init() {
        TrinamicUartDriver::init();
        if (!_uart) {
            return;
        }

        if (_r_sense == 0) {
            _r_sense = TMC2209_RSENSE_DEFAULT;
        }

        tmc2209 = new TMC2209Stepper(_uart, _r_sense, _addr);

        registration();
    }

    void TMC2209Driver::config_motor() {
        _cs_pin.synchronousWrite(true);
        tmc2209->begin();
        TrinamicBase::config_motor();
        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        TrinamicMode _mode = static_cast<TrinamicMode>(trinamicModes[isHoming ? _homing_mode : _run_mode].value);

        // Run and hold current configuration items are in (float) Amps,
        // but the TMCStepper library expresses run current as (uint16_t) mA
        // and hold current as (float) fraction of run current.
        float    _mode_current = isHoming ? _homing_current : _run_current;
        uint16_t run_i         = (uint16_t)(_mode_current * 1000.0);

        _cs_pin.synchronousWrite(true);

        tmc2209->I_scale_analog(false);  // do not scale via pot
        tmc2209->rms_current(run_i, TrinamicBase::holdPercent());

        // The TMCStepper library uses the value 0 to mean 1x microstepping
        uint32_t usteps = _microsteps == 1 ? 0 : _microsteps;
        tmc2209->microsteps(usteps);
        tmc2209->pdn_disable(true);  // powerdown pin is disabled. uses ihold.

        switch (_mode) {
            case TrinamicMode ::StealthChop:
                log_debug(axisName() << " StealthChop");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(true);
                break;
            case TrinamicMode ::CoolStep:
                log_debug(axisName() << " Coolstep");
                tmc2209->en_spreadCycle(true);
                tmc2209->pwm_autoscale(false);
                break;
            case TrinamicMode ::StallGuard:  //TODO: check all configurations for stallguard
            {
                log_debug(axisName() << " Stallguard");
                tmc2209->en_spreadCycle(false);
                tmc2209->pwm_autoscale(true);
                tmc2209->TCOOLTHRS(calc_tstep(150));
                tmc2209->SGTHRS(_stallguard);
                break;
            }
        }

        // Most TMCStepper setters above use local shadow registers and are
        // write-only.  Several getters below read the device, so omit the dump
        // when duplicate responders make readback electrically ambiguous.
        if (tmc2209RequiresReadback(_shared_address_write_only)) {
            log_verbose("CHOPCONF: " << to_hex(tmc2209->CHOPCONF()));
            log_verbose("COOLCONF: " << to_hex(tmc2209->COOLCONF()));
            log_verbose("TPWMTHRS: " << to_hex(tmc2209->TPWMTHRS()));
            log_verbose("TCOOLTHRS: " << to_hex(tmc2209->TCOOLTHRS()));
            log_verbose("GCONF: " << to_hex(tmc2209->GCONF()));
            log_verbose("PWMCONF: " << to_hex(tmc2209->PWMCONF()));
            log_verbose("IHOLD_IRUN: " << to_hex(tmc2209->IHOLD_IRUN()));
        }

        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::debug_message() {
        if (_has_errors || _shared_address_write_only) {
            return;
        }

        _cs_pin.synchronousWrite(true);

        uint32_t tstep = tmc2209->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            _cs_pin.synchronousWrite(false);
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        if (tmc2209) {
            log_info(axisName() << " SG_Val: " << tmc2209->SG_RESULT() << "   Rate: " << feedrate << " mm/min SG_Setting:" << _stallguard);
        }

        _cs_pin.synchronousWrite(false);
    }

    void TMC2209Driver::set_disable(bool disable) {
        if (TrinamicUartDriver::startDisable(disable)) {
            if (_use_enable) {
                _cs_pin.synchronousWrite(true);
                tmc2209->toff(TrinamicUartDriver::toffValue());
                _cs_pin.synchronousWrite(false);
            }
        }
    }

    bool TMC2209Driver::test() {
        if (_shared_address_write_only) {
            log_warn(axisName() << " TMC2209 shared-address write-only mode; UART readback and diagnostics are unavailable");
            return true;
        }

        _cs_pin.synchronousWrite(true);
        if (!checkVersion(0x21, tmc2209->version())) {
            _cs_pin.synchronousWrite(false);
            return false;
        }

        uint8_t ifcnt_before = tmc2209->IFCNT();
        tmc2209->GSTAT(0);  // clear GSTAT to increase ifcnt
        uint8_t ifcnt_after = tmc2209->IFCNT();
        bool    okay        = ((ifcnt_before + 1) & 0xff) == ifcnt_after;
        if (!okay) {
            TrinamicBase::reportCommsFailure();
            _cs_pin.synchronousWrite(false);
            return false;
        }
        _cs_pin.synchronousWrite(false);
        return true;
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2209Driver> registration("tmc_2209");
    }
}
