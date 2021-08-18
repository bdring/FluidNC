/*
  System.cpp - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modified for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

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

#include "System.h"

#include "MotionControl.h"  // motors_to_cartesian
#include "Protocol.h"       // protocol_buffer_synchronize
#include "Config.h"
#include "UserOutput.h"
#include "SettingsDefinitions.h"
#include "Machine/MachineConfig.h"

#include <atomic>
#include <cstring>  // memset
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp32-hal-gpio.h>  // LOW

// Declare system global variable structure
system_t sys;
int32_t  motor_steps[MAX_N_AXIS];  // Real-time position in steps.
int32_t  probe_steps[MAX_N_AXIS];  // Last probe position in steps.

void system_reset() {
    // Reset system variables.
    State prior_state = sys.state;
    memset(&sys, 0, sizeof(system_t));  // Clear system struct variable.
    sys.state             = prior_state;
    sys.f_override        = FeedOverride::Default;          // Set to 100%
    sys.r_override        = RapidOverride::Default;         // Set to 100%
    sys.spindle_speed_ovr = SpindleSpeedOverride::Default;  // Set to 100%
    memset(probe_steps, 0, sizeof(probe_steps));            // Clear probe position.
    report_ovr_counter = 0;
    report_wco_counter = 0;
}

float steps_to_mpos(int32_t steps, uint8_t axis) {
    return float(steps / config->_axes->_axis[axis]->_stepsPerMm);
}
int32_t mpos_to_steps(float mpos, size_t axis) {
    return int32_t(mpos * config->_axes->_axis[axis]->_stepsPerMm);
}

void motor_steps_to_mpos(float* position, int32_t* steps) {
    auto  n_axis = config->_axes->_numberAxis;
    float motor_mpos[MAX_N_AXIS];
    for (int idx = 0; idx < n_axis; idx++) {
        motor_mpos[idx] = steps_to_mpos(steps[idx], idx);
    }
    motors_to_cartesian(position, motor_mpos, n_axis);
}

float* get_mpos() {
    static float position[MAX_N_AXIS];
    motor_steps_to_mpos(position, motor_steps);
    return position;
};

std::map<State, const char*> StateName = {
    { State::Idle, "Idle" },
    { State::Alarm, "Alarm" },
    { State::CheckMode, "CheckMode" },
    { State::Homing, "Homing" },
    { State::Cycle, "Cycle" },
    { State::Hold, "Hold" },
    { State::Jog, "Jog" },
    { State::SafetyDoor, "SafetyDoor" },
    { State::Sleep, "Sleep" },
    { State::ConfigAlarm, "ConfigAlarm" },
};
