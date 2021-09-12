// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Planner.h"
#include "NutsBolts.h"

#include <cstdint>

extern volatile ProbeState probeState;  // Probing state value.  Used to coordinate the probing cycle with stepper ISR.

extern bool probe_succeeded;  // Tracks if last probing cycle was successful.

// System motion commands must have a line number of zero.
const int PARKING_MOTION_LINE_NUMBER = 0;

// Execute linear motion in absolute millimeter coordinates. Feed rate given in millimeters/second
// unless invert_feed_rate is true. Then the feed_rate means that the motion should be completed in
// (1 minute)/feed_rate time.
bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position);
void motors_to_cartesian(float* cartesian, float* motors, int n_axis);
bool mc_line(float* target, plan_line_data_t* pl_data);  // returns true if line was submitted to planner

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
            bool              is_clockwise_arc);

// Dwell for a specific number of seconds
bool mc_dwell(int32_t milliseconds);

// Perform homing cycle to locate machine zero. Requires limit switches.
void mc_homing_cycle(AxisMask cycle_mask);

// Perform tool length probe cycle. Requires probe switch.
GCUpdatePos mc_probe_cycle(float* target, plan_line_data_t* pl_data, uint8_t parser_flags);

// Handles updating the override control state.
void mc_override_ctrl_update(Override override_state);

// Plans and executes the single special motion case for parking. Independent of main planner buffer.
void mc_parking_motion(float* parking_target, plan_line_data_t* pl_data);

// Performs system reset. If in motion state, kills all motion and sets system alarm.
void mc_reset();

void mc_cancel_jog();

void mc_init();

bool kinematics_pre_homing(AxisMask cycle_mask);
void kinematics_post_homing();
