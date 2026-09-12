// Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "System.h"
#include "Planner.h"  // plan_line_data_t

#include <cstdint>

extern bool soft_limit;

// Initialize the limits module
void limits_init();

// Returns limit state
MotorMask limits_get_state();

void limit_error();

// axis is the axis whose limit was exceeded, coordinate is the out-of-range value.
// target, if non-null, is the full commanded mpos (all axes) for the move that was
// rejected, and pl_data, if non-null, carries the gcode line number for that move.
void limit_error(axis_t axis, float coordinate, float* target = nullptr, plan_line_data_t* pl_data = nullptr);

// Reports a soft limit failure that isn't tied to a single axis/bound, e.g. a
// commanded position that inverse kinematics cannot reach at all (ParallelDelta).
void limit_error(float* target, plan_line_data_t* pl_data);

float limitsMaxPosition(axis_t axis);
float limitsMinPosition(axis_t axis);

// Private

#ifdef LATER  // We need to rethink debouncing
// A task that runs after a limit switch interrupt.
void limitCheckTask(void* pvParameters);
#endif

// True if an axis is reporting engaged limits on both ends.  This
// typically happens when the same pin is used for a pair of switches,
// so you cannot tell which one is triggered.  In that case, automatic
// pull-off is impossible.
bool ambiguousLimit();
