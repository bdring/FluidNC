// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Types.h"  // AxisMask
#include "Planner.h"
#include "Config.h"
#include "Probe.h"

#include <cstdint>

extern volatile ProbeState probeState;  // Probing state value.  Used to coordinate the probing cycle with stepper ISR.

extern bool probe_succeeded;  // Tracks if last probing cycle was successful.

// System motion commands must have a line number of zero.
const int PARKING_MOTION_LINE_NUMBER = 0;

// Execute a linear motion in cartesian space.
bool mc_linear(float* target, plan_line_data_t* pl_data, float* position);

// Execute a linear motion in motor space.
bool mc_move_motors(float* target, plan_line_data_t* pl_data);  // returns true if line was submitted to planner

// Execute an arc in offset mode format. position == current xyz, target == target xyz,
// offset == offset from current xyz, axis_XXX defines circle plane in tool space, axis_linear is
// the direction of helical travel, radius == circle radius, is_clockwise_arc boolean. Used
// for vector transformation direction.
void mc_arc(float*            target,
            plan_line_data_t* pl_data,
            float*            position,
            float*            offset,
            float             radius,
            size_t            axis_0,
            size_t            axis_1,
            size_t            axis_linear,
            bool              is_clockwise_arc,
            int               pword_rotations);

// Dwell for a specific number of seconds
bool mc_dwell(int32_t milliseconds);

// Perform tool length probe cycle. Requires probe switch.
GCUpdatePos mc_probe_cycle(float* target, plan_line_data_t* pl_data, bool away, bool no_error, uint8_t offsetAxis, float offset);

// Handles updating the override control state.
void mc_override_ctrl_update(Override override_state);

// Performs system reset. If in motion state, kills all motion and sets system alarm.
void mc_critical(ExecAlarm alarm);

void mc_cancel_jog();

void mc_init();
