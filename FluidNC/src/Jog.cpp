/*
  Jog.cpp - Jogging methods
  Part of Grbl

  Copyright (c) 2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
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

#include "Jog.h"

#include "Machine/MachineConfig.h"
#include "Limits.h"         // limitsCheckTravel
#include "MotionControl.h"  // cartesian_to_motors
#include "Stepper.h"        // st_prep_buffer, st_wake_up

// Sets up valid jog motion received from g-code parser, checks for soft-limits, and executes the jog.
// cancelledInflight will be set to true if was not added to parser due to a cancelJog.
Error jog_execute(plan_line_data_t* pl_data, parser_block_t* gc_block, bool* cancelledInflight) {
    // Initialize planner data struct for jogging motions.
    // NOTE: Spindle and coolant are allowed to fully function with overrides during a jog.
    pl_data->feed_rate             = gc_block->values.f;
    pl_data->motion.noFeedOverride = 1;
    pl_data->is_jog                = true;
    pl_data->line_number           = gc_block->values.n;

    if (config->_axes->hasSoftLimits()) {
        if (limitsCheckTravel(gc_block->values.xyz)) {
            return Error::TravelExceeded;
        }
    }
    // Valid jog command. Plan, set state, and execute.
    if (!cartesian_to_motors(gc_block->values.xyz, pl_data, gc_state.position)) {
        return Error::JogCancelled;
    }

    if (sys.state == State::Idle) {
        if (plan_get_current_block() != NULL) {  // Check if there is a block to execute.
            sys.state = State::Jog;
            Stepper::prep_buffer();
            Stepper::wake_up();  // NOTE: Manual start. No state machine required.
        }
    }
    return Error::Ok;
}
