// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring This file was modified for use on the ESP32
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  System.cpp - Header for system level commands and real-time processes
*/

#include "System.h"
#include "Report.h"                 // report_ovr_counter
#include "Config.h"                 // MAX_N_AXIS
#include "Machine/MachineConfig.h"  // config

#include <cstring>  // memset

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

float steps_to_mpos(int32_t steps, size_t axis) {
    return float(steps / config->_axes->_axis[axis]->_stepsPerMm);
}
int32_t mpos_to_steps(float mpos, size_t axis) {
    return int32_t(mpos * config->_axes->_axis[axis]->_stepsPerMm);
}

void motor_steps_to_mpos(float* position, int32_t* steps) {
    float motor_mpos[MAX_N_AXIS];
    auto  a      = config->_axes;
    auto  n_axis = a ? a->_numberAxis : 0;
    for (size_t idx = 0; idx < n_axis; idx++) {
        motor_mpos[idx] = steps_to_mpos(steps[idx], idx);
    }
    config->_kinematics->motors_to_cartesian(position, motor_mpos, n_axis);
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
