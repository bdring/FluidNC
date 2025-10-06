// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  System.cpp - Header for system level commands and real-time processes
*/

#include "System.h"
#include "Report.h"                 // report_ovr_counter
#include "Config.h"                 // MAX_N_AXIS
#include "Machine/MachineConfig.h"  // config
#include "Stepping.h"               // config

#include <cstring>  // memset
#include <cmath>    // roundf

// Declare system global variable structure
system_t sys;
int32_t  probe_steps[MAX_N_AXIS];  // Last probe position in steps.

void system_reset() {
    // Reset system variables.
    State prior_state = sys.state();
    bool  prior_abort = sys.abort();
    sys.reset();  // Clear system struct variable.
    set_state(prior_state);
    sys.set_abort(prior_abort);
    sys.set_f_override(FeedOverride::Default);                 // Set to 100%
    sys.set_r_override(RapidOverride::Default);                // Set to 100%
    sys.set_spindle_speed_ovr(SpindleSpeedOverride::Default);  // Set to 100%
    memset(probe_steps, 0, sizeof(probe_steps));               // Clear probe position.
    report_ovr_counter = 0;
    report_wco_counter = 0;
}

float steps_to_mpos(int32_t steps, axis_t axis) {
    return float(steps / Axes::_axis[axis]->_stepsPerMm);
}
int32_t mpos_to_steps(float mpos, axis_t axis) {
    return lroundf(mpos * Axes::_axis[axis]->_stepsPerMm);
}

void motor_steps_to_mpos(float* position, int32_t* steps) {
    float  motor_mpos[MAX_N_AXIS];
    auto   a      = config->_axes;
    axis_t n_axis = a ? a->_numberAxis : X_AXIS;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        motor_mpos[axis] = steps_to_mpos(steps[axis], axis);
    }
    config->_kinematics->motors_to_cartesian(position, motor_mpos, n_axis);
}

void set_motor_steps(axis_t axis, int32_t steps) {
    Stepping::setSteps(axis, steps);
}

void set_motor_steps_from_mpos(float* mpos) {
    auto  n_axis = Axes::_numberAxis;
    float motor_steps[n_axis];
    config->_kinematics->transform_cartesian_to_motors(motor_steps, mpos);
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        set_motor_steps(axis, mpos_to_steps(motor_steps[axis], axis));
    }
}

int32_t get_axis_motor_steps(axis_t axis) {
    return Stepping::getSteps(axis);
}

void get_motor_steps(int32_t* motor_steps) {
    auto axes   = config->_axes;
    auto n_axis = axes->_numberAxis;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        motor_steps[axis] = Stepping::getSteps(axis);
    }
}
int32_t* get_motor_steps() {
    static int32_t motor_steps[MAX_N_AXIS];

    get_motor_steps(motor_steps);
    return motor_steps;
}

float* get_mpos() {
    static float position[MAX_N_AXIS];

    motor_steps_to_mpos(position, get_motor_steps());
    return position;
};

float* get_wco() {
    static float wco[MAX_N_AXIS];
    auto         n_axis = Axes::_numberAxis;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        // Apply work coordinate offsets and tool length offset to current position.
        wco[axis] = gc_state.coord_system[axis] + gc_state.coord_offset[axis];
        if (axis == TOOL_LENGTH_OFFSET_AXIS) {
            wco[axis] += gc_state.tool_length_offset;
        }
    }
    return wco;
}

const std::map<State, const char*> StateName = {
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
    { State::Critical, "Critical" },
};

void set_state(State s) {
    sys.set_state(s);
}
bool state_is(State s) {
    return sys.state() == s;
}

bool inMotionState() {
    return state_is(State::Cycle) || state_is(State::Homing) || state_is(State::Jog) ||
           (state_is(State::Hold) && !sys.suspend().bit.holdComplete);
}
