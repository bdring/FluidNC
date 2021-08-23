#pragma once

/*
  Stepper.h - stepper motor driver: executes motion plans of planner.c using the stepper motors
  Part of Grbl

  Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

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

#include "EnumItem.h"

#include <cstdint>

namespace Stepper {
    void init();

    void pulse_func();

    // Enable steppers, but cycle does not start unless called by motion control or realtime command.
    void wake_up();

    // Stops stepping
    void go_idle();

    // Reset the stepper subsystem variables
    void reset();

    // Changes the run state of the step segment buffer to execute the special parking motion.
    void parking_setup_buffer();

    // Restores the step segment buffer to the normal run state after a parking motion.
    void parking_restore_buffer();

    // Reloads step segment buffer. Called continuously by realtime execution system.
    void prep_buffer();

    // Called by planner_recalculate() when the executing block is updated by the new plan.
    void update_plan_block_parameters();

    // Called by realtime status reporting if realtime rate reporting is enabled in config.h.
    float get_realtime_rate();

    extern uint32_t isr_count;  // for debugging only
}
// private
