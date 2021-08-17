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
system_t               sys;
int32_t                sys_position[MAX_N_AXIS];        // Real-time machine (aka home) position vector in steps.
int32_t                sys_probe_position[MAX_N_AXIS];  // Last probe position in machine coordinates and steps.
volatile ProbeState    sys_probe_state;                 // Probing state value.  Used to coordinate the probing cycle with stepper ISR.
volatile ExecAlarm     sys_rt_exec_alarm;               // Global realtime executor bitflag variable for setting various alarms.
volatile ExecAccessory sys_rt_exec_accessory_override;  // Global realtime executor bitflag variable for spindle/coolant overrides.
volatile bool          rtStatusReport;
volatile bool          rtCycleStart;
volatile bool          rtFeedHold;
volatile bool          rtReset;
volatile bool          rtSafetyDoor;
volatile bool          rtMotionCancel;
volatile bool          rtSleep;
volatile bool          rtCycleStop;  // For state transitions, instead of bitflag
volatile bool          rtButtonMacro0;
volatile bool          rtButtonMacro1;
volatile bool          rtButtonMacro2;
volatile bool          rtButtonMacro3;
#ifdef DEBUG_REPORT_REALTIME
volatile bool sys_rt_exec_debug;
#endif
volatile Percent sys_rt_f_override;  // Global realtime executor feedrate override percentage
volatile Percent sys_rt_r_override;  // Global realtime executor rapid override percentage
volatile Percent sys_rt_s_override;  // Global realtime executor spindle override percentage

xQueueHandle control_sw_queue;    // used by control switch debouncing
bool         debouncing = false;  // debouncing in process

void system_reset() {
    // Reset system variables.
    State prior_state = sys.state;
    memset(&sys, 0, sizeof(system_t));  // Clear system struct variable.
    sys.state             = prior_state;
    sys.f_override        = FeedOverride::Default;              // Set to 100%
    sys.r_override        = RapidOverride::Default;             // Set to 100%
    sys.spindle_speed_ovr = SpindleSpeedOverride::Default;      // Set to 100%
    memset(sys_probe_position, 0, sizeof(sys_probe_position));  // Clear probe position.
}

void system_flag_wco_change() {
    if (FORCE_BUFFER_SYNC_DURING_WCO_CHANGE) {
        protocol_buffer_synchronize();
    }
    sys.report_wco_counter = 0;
}

float system_convert_axis_steps_to_mpos(int32_t* steps, uint8_t idx) {
    float pos;
    float steps_per_mm = config->_axes->_axis[idx]->_stepsPerMm;
    pos                = steps[idx] / steps_per_mm;
    return pos;
}

void system_convert_array_steps_to_mpos(float* position, int32_t* steps) {
    auto  n_axis = config->_axes->_numberAxis;
    float motors[MAX_N_AXIS];
    for (int idx = 0; idx < n_axis; idx++) {
        motors[idx] = (float)steps[idx] / config->_axes->_axis[idx]->_stepsPerMm;
    }
    motors_to_cartesian(position, motors, n_axis);
}

float* system_get_mpos() {
    static float position[MAX_N_AXIS];
    system_convert_array_steps_to_mpos(position, sys_position);
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
