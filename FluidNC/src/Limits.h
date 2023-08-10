// Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "System.h"

#include <cstdint>

extern bool soft_limit;

// Initialize the limits module
void limits_init();

// Returns limit state
MotorMask limits_get_state();

void limit_error();
void limit_error(size_t axis, float cordinate);

float limitsMaxPosition(size_t axis);
float limitsMinPosition(size_t axis);

// Private

// Returns limit state under mask
AxisMask limits_check(AxisMask check_mask);

// A task that runs after a limit switch interrupt.
void limitCheckTask(void* pvParameters);

bool limitsCheckTravel(float* target);

// True if an axis is reporting engaged limits on both ends.  This
// typically happens when the same pin is used for a pair of switches,
// so you cannot tell which one is triggered.  In that case, automatic
// pull-off is impossible.
bool ambiguousLimit();
