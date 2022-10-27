// Copyright (c) 2021 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TrinamicBase.h"
#include "../Machine/MachineConfig.h"

#include <atomic>

namespace MotorDrivers {
    EnumItem trinamicModes[] = { { TrinamicMode::StealthChop, "StealthChop" },
                                 { TrinamicMode::CoolStep, "CoolStep" },
                                 { TrinamicMode::StallGuard, "StallGuard" },
                                 EnumItem(TrinamicMode::StealthChop) };

    TrinamicBase* TrinamicBase::List = NULL;  // a static list of all drivers for stallguard reporting

    // Prints StallGuard data that is useful for tuning.
    void TrinamicBase::readSgTask(void* pvParameters) {
        auto trinamicDriver = static_cast<TrinamicBase*>(pvParameters);

        TickType_t       xLastWakeTime;
        const TickType_t xreadSg = 200;  // in ticks (typically ms)
        auto             n_axis  = config->_axes->_numberAxis;

        xLastWakeTime = xTaskGetTickCount();  // Initialise the xLastWakeTime variable with the current time.
        while (true) {                        // don't ever return from this or the task dies
            std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
            if (inMotionState()) {
                for (TrinamicBase* p = List; p; p = p->link) {
                    if (p->_stallguardDebugMode) {
                        //log_info("SG:" << p->_stallguardDebugMode);
                        p->debug_message();
                    }
                }
            }  // sys.state

            vTaskDelayUntil(&xLastWakeTime, xreadSg);

            static UBaseType_t uxHighWaterMark = 0;
#ifdef DEBUG_TASK_STACK
            reportTaskStackSize(uxHighWaterMark);
#endif
        }
    }

    // calculate a tstep from a rate
    // tstep = fclk / (time between 1/256 steps)
    // This is used to set the stallguard window from the homing speed.
    // The percent is the offset on the window
    uint32_t TrinamicBase::calc_tstep(float speed, float percent) {
        double tstep = speed / 60.0 * config->_axes->_axis[axis_index()]->_stepsPerMm * (256.0 / _microsteps);
        tstep        = fclk / tstep * percent / 100.0;

        return static_cast<uint32_t>(tstep);
    }

    // =========== Reporting functions ========================

    bool TrinamicBase::report_open_load(bool ola, bool olb) {
        if (ola || olb) {
            log_warn("    Driver Open Load a:" << yn(ola) << " b:" << yn(olb));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ground(bool s2ga, bool s2gb) {
        if (s2ga || s2gb) {
            log_warn("    Driver Short Coil a:" << yn(s2ga) << " b:" << yn(s2gb));
        }
        return false;  // no error
    }

    bool TrinamicBase::report_over_temp(bool ot, bool otpw) {
        if (ot || otpw) {
            log_warn("    Driver Temp Warning:" << yn(otpw) << " Fault:" << yn(ot));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ps(bool vsa, bool vsb) {
        // check for short to power supply
        if (vsa || vsb) {
            log_warn("    Driver Short vsa:" << yn(vsa) << " vsb:" << yn(vsb));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::set_homing_mode(bool isHoming) {
        set_registers(isHoming);
        return true;
    }

    float TrinamicBase::holdPercent() {
        if (_run_current == 0) {
            return 0.0;
        }

        float hold_percent = _hold_current / _run_current;
        if (hold_percent > 1.0) {
            hold_percent = 1.0;
        }

        return hold_percent;
    }

    bool TrinamicBase::reportTest(uint8_t result) {
        switch (result) {
            case 1:
                log_error(axisName() << " driver test failed. Check connection");
                return false;
            case 2:
                log_error(axisName() << " driver test failed. Check motor power");
                return false;
            default:
                log_info(axisName() << " driver test passed");
                return true;
        }
    }

    bool TrinamicBase::checkVersion(uint8_t expected, uint8_t got) {
        if (expected != got) {
            log_error(axisName() << " TMC driver not detected - expected 0x" << String(expected, 16) << " got 0x" << String(got, 16));
            return false;
        }
        log_info(axisName() << " driver test passed");
        return true;
    }

    void TrinamicBase::reportCommsFailure(void) { log_info(axisName() << " communications check failed"); }

    bool TrinamicBase::startDisable(bool disable) {
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

    void TrinamicBase::config_motor() {
        _has_errors = !test();  // Try communicating with motor. Prints an error if there is a problem.

        init_step_dir_pins();

        if (_has_errors) {
            return;
        }

        set_registers(false);
    }
}
