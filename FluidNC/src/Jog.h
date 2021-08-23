// Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Error.h"
#include "Planner.h"
#include "GCode.h"

// System motion line numbers must be zero.
const int JOG_LINE_NUMBER = 0;

// Sets up valid jog motion received from g-code parser, checks for soft-limits, and executes the jog.
// cancelledInflight will be set to true if was not added to parser due to a cancelJog.
Error jog_execute(plan_line_data_t* pl_data, parser_block_t* gc_block, bool* cancelledInflight);
