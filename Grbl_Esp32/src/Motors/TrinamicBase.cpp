/*
    TrinamicBase.cpp

    Part of Grbl_ESP32
    2021 - Stefan de Bruijn

    Grbl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Grbl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "TrinamicBase.h"

#include "../Machine/MachineConfig.h"

#include <atomic>

namespace MotorDrivers {
    EnumItem trinamicModes[] = { { TrinamicMode::Unknown, "Unknown" },
                                 { TrinamicMode::StealthChop, "StealthChop" },
                                 { TrinamicMode::CoolStep, "CoolStep" },
                                 { TrinamicMode::StallGuard, "StallGuard" },
                                 EnumItem(TrinamicMode::StealthChop) };

    TrinamicBase* TrinamicBase::List = NULL;  // a static list of all drivers for stallguard reporting

    uint8_t TrinamicBase::get_next_index() {
        static uint8_t index = 1;  // they start at 1
        return index++;
    }

    // Prints StallGuard data that is useful for tuning.
    void TrinamicBase::readSgTask(void* pvParameters) {
        auto trinamicDriver = static_cast<TrinamicBase*>(pvParameters);

        TickType_t       xLastWakeTime;
        const TickType_t xreadSg = 200;  // in ticks (typically ms)
        auto             n_axis  = config->_axes->_numberAxis;

        xLastWakeTime = xTaskGetTickCount();  // Initialise the xLastWakeTime variable with the current time.
        while (true) {                        // don't ever return from this or the task dies
            std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
            if (sys.state == State::Cycle || sys.state == State::Homing || sys.state == State::Jog) {
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
            log_info(axisName() << " Driver Open Load a:" << yn(ola) << " b:" << yn(olb));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ground(bool s2ga, bool s2gb) {
        if (s2ga || s2gb) {
            log_info(axisName() << " Driver Short Coil a:" << yn(s2ga) << " b:" << yn(s2gb));
        }
        return false;  // no error
    }

    bool TrinamicBase::report_over_temp(bool ot, bool otpw) {
        if (ot || otpw) {
            log_info(axisName() << " Driver Temp Warning:" << yn(otpw) << " Fault:" << yn(ot));
            return true;
        }
        return false;  // no error
    }

    bool TrinamicBase::report_short_to_ps(bool vsa, bool vsb) {
        // check for short to power supply
        if (vsa || vsb) {
            log_info(axisName() << " Driver Short vsa:" << yn(vsa) << " vsb:" << yn(vsb));
            return true;
        }
        return false;  // no error
    }
}
