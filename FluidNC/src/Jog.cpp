// Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Jog.h"

#include "Machine/MachineConfig.h"
#include "MotionControl.h"  // mc_linear
#include "Stepper.h"        // st_prep_buffer, st_wake_up
#include "Limits.h"         // constrainToSoftLimits()

// Sets up valid jog motion received from g-code parser, checks for soft-limits, and executes the jog.
// cancelledInflight will be set to true if was not added to parser due to a cancelJog.
Error jog_execute(plan_line_data_t* pl_data, parser_block_t* gc_block, bool* cancelledInflight) {
    config->_kinematics->constrain_jog(gc_block->values.xyz, pl_data, gc_state.position);

    // Initialize planner data struct for jogging motions.
    // NOTE: Spindle and coolant are allowed to fully function with overrides during a jog.
    pl_data->feed_rate             = gc_block->values.f;
    pl_data->motion.noFeedOverride = 1;
    pl_data->is_jog                = true;
    pl_data->line_number           = gc_block->values.n;

    if (!mc_linear(gc_block->values.xyz, pl_data, gc_state.position)) {
        return Error::JogCancelled;
    }

    // The motion will be initiated by the cycle start mechanism
    return Error::Ok;
}
