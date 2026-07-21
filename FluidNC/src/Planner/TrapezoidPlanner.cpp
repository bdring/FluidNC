// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2011 Jens Geisler
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TrapezoidPlanner.h"
#include "../Planner.h"
#include "../Stepper.h"
#include "../StepperPrivate.h"
#include "../System.h"

#include <cmath>
#include <algorithm>

/*                            PLANNER SPEED DEFINITION
                                     +--------+   <- current->nominal_speed
                                    /          \
         current->entry_speed ->   +            \
                                   |             + <- next->entry_speed (aka exit speed)
                                   +-------------+
                                       time -->

  Recalculates the motion plan according to the following basic guidelines:

    1. Go over every feasible block sequentially in reverse order and calculate the junction speeds
        (i.e. current->entry_speed) such that:
      a. No junction speed exceeds the pre-computed maximum junction speed limit or nominal speeds of
         neighboring blocks.
      b. A block entry speed cannot exceed one reverse-computed from its exit speed (next->entry_speed)
         with a maximum allowable deceleration over the block travel distance.
      c. The last (or newest appended) block is planned from a complete stop (an exit speed of zero).
    2. Go over every block in chronological (forward) order and dial down junction speed values if
      a. The exit speed exceeds the one forward-computed from its entry speed with the maximum allowable
         acceleration over the block travel distance.

  When these stages are complete, the planner will have maximized the velocity profiles throughout the all
  of the planner blocks, where every block is operating at its maximum allowable acceleration limits. In
  other words, for all of the blocks in the planner, the plan is optimal and no further speed improvements
  are possible. If a new block is added to the buffer, the plan is recomputed according to the said
  guidelines for a new optimal plan.

  To increase computational efficiency of these guidelines, a set of planner block pointers have been
  created to indicate stop-compute points for when the planner guidelines cannot logically make any further
  changes or improvements to the plan when in normal operation and new blocks are streamed and added to the
  planner buffer. For example, if a subset of sequential blocks in the planner have been planned and are
  bracketed by junction velocities at their maximums (or by the first planner block as well), no new block
  added to the planner buffer will alter the velocity profiles within them. So we no longer have to compute
  them. Or, if a set of sequential blocks from the first block in the planner (or a optimal stop-compute
  point) are all accelerating, they are all optimal and can not be altered by a new block added to the
  planner buffer, as this will only further increase the plan speed to chronological blocks until a maximum
  junction velocity is reached. However, if the operational conditions of the plan changes from infrequently
  used feed holds or feedrate overrides, the stop-compute pointers will be reset and the entire plan is
  recomputed as stated in the general guidelines.

  Planner buffer index mapping:
  - block_buffer_tail: Points to the beginning of the planner buffer. First to be executed or being executed.
  - block_buffer_head: Points to the buffer block after the last block in the buffer. Used to indicate whether
      the buffer is full or empty. As described for standard ring buffers, this block is always empty.
  - next_buffer_head: Points to next planner buffer block after the buffer head block. When equal to the
      buffer tail, this indicates the buffer is full.
  - block_buffer_planned: Points to the first buffer block after the last optimally planned block for normal
      streaming operating conditions. Use for planning optimizations by avoiding recomputing parts of the
      planner buffer that don't change with the addition of a new block, as describe above. In addition,
      this block can never be less than block_buffer_tail and will always be pushed forward and maintain
      this requirement when encountered by the plan_discard_current_block() routine during a cycle.

  NOTE: Since the planner only computes on what's in the planner buffer, some motions with lots of short
  line segments, like G2/3 arcs or complex curves, may seem to move slow. This is because there simply isn't
  enough combined distance traveled in the entire buffer to accelerate up to the nominal speed and then
  decelerate to a complete stop at the end of the buffer, as stated by the guidelines. If this happens and
  becomes an annoyance, there are a few simple solutions: (1) Maximize the machine acceleration. The planner
  will be able to compute higher velocity profiles within the same combined distance. (2) Maximize line
  motion(s) distance per block to a desired tolerance. The more combined distance the planner has to use,
  the faster it can go. (3) Maximize the planner buffer size. This also will increase the combined distance
  for the planner to compute over. It also increases the number of computations the planner has to perform
  to compute an optimal plan, so select carefully.
*/

void TrapezoidPlanner::recalculate() {
    recalculateBackward();
    recalculateForward();
}

// Reverse Pass: Coarsely maximize all possible deceleration curves back-planning from the last
// block in buffer. Cease planning when the last optimal planned or tail pointer is reached.
// NOTE: Forward pass will later refine and correct the reverse pass to create an optimal plan.
// Reverse Pass: Coarsely maximize all possible deceleration curves back-planning from the last
// block in buffer. Cease planning when the last optimal planned or tail pointer is reached.
// NOTE: Forward pass will later refine and correct the reverse pass to create an optimal plan.
void TrapezoidPlanner::recalculateBackward() {
    // Initialize block index to the last block in the planner buffer.
    uint8_t block_index = prevBlockIndex(_block_buffer_head);

    // Bail. Can't do anything with only one plan-able block.
    if (block_index == _block_buffer_planned) {
        return;
    }

    plan_block_t* current = &_block_buffer[block_index];

    // Calculate maximum entry speed for last block in buffer, where the exit speed is always zero.
    // Using trapezoidal kinematics: v² = v₀² + 2ad → v₀² = v² - 2ad = 0 - 2*(-a)*d = 2ad
    current->entry_speed_sqr = std::min(current->max_entry_speed_sqr, 2.0f * current->acceleration * current->millimeters);

    block_index = prevBlockIndex(block_index);

    if (block_index == _block_buffer_planned) {
        // Only two plannable blocks in buffer. Reverse pass complete.
        // Check if the first block is the tail. If so, notify stepper to update its current parameters.
        if (block_index == _block_buffer_tail) {
            Stepper::update_plan_block_parameters();
        }
    } else {
        // Three or more plan-able blocks
        plan_block_t* next;
        while (block_index != _block_buffer_planned) {
            next        = current;
            current     = &_block_buffer[block_index];
            block_index = prevBlockIndex(block_index);

            // Check if next block is the tail block(=planned block). If so, update current stepper parameters.
            if (block_index == _block_buffer_tail) {
                Stepper::update_plan_block_parameters();
            }

            // Compute maximum entry speed decelerating over the current block from its exit speed.
            // The exit_speed of current block = entry_speed of next block
            if (current->entry_speed_sqr != current->max_entry_speed_sqr) {
                // v₀² = v² + 2ad (solving backward: what entry speed allows decel to exit speed?)
                float entry_speed_sqr = next->entry_speed_sqr + 2.0f * current->acceleration * current->millimeters;
                if (entry_speed_sqr < current->max_entry_speed_sqr) {
                    current->entry_speed_sqr = entry_speed_sqr;
                } else {
                    current->entry_speed_sqr = current->max_entry_speed_sqr;
                }
            }
        }
    }
}

// Forward Pass: Forward plan the acceleration curve from the planned pointer onward.
// Also scans for optimal plan breakpoints and appropriately updates the planned pointer.
void TrapezoidPlanner::recalculateForward() {
    if (_block_buffer_head == _block_buffer_tail) {
        return;
    }

    plan_block_t* next        = &_block_buffer[_block_buffer_planned];  // Begin at buffer planned pointer
    uint8_t       block_index = nextBlockIndex(_block_buffer_planned);

    while (block_index != _block_buffer_head) {
        plan_block_t* current = next;
        next                  = &_block_buffer[block_index];

        // Any acceleration detected in the forward pass automatically moves the optimal planned
        // pointer forward, since everything before this is all optimal. In other words, nothing
        // can improve the plan from the buffer tail to the planned pointer by logic.
        if (current->entry_speed_sqr < next->entry_speed_sqr) {
            // Compute achievable exit speed: v² = v₀² + 2ad
            float entry_speed_sqr = current->entry_speed_sqr + 2.0f * current->acceleration * current->millimeters;

            // If true, current block is full-acceleration and we can move the planned pointer forward.
            if (entry_speed_sqr < next->entry_speed_sqr) {
                next->entry_speed_sqr = entry_speed_sqr;  // Always <= max_entry_speed_sqr. Backward pass sets this.
                _block_buffer_planned = block_index;      // Set optimal plan pointer.
            }
        }

        // Any block set at its maximum entry speed also creates an optimal plan up to this
        // point in the buffer. When the plan is bracketed by either the beginning of the
        // buffer and a maximum entry speed or two maximum entry speeds, every block in between
        // cannot logically be further improved. Hence, we don't have to recompute them anymore.
        if (next->entry_speed_sqr == next->max_entry_speed_sqr) {
            _block_buffer_planned = block_index;
        }

        block_index = nextBlockIndex(block_index);
    }
}

TrapezoidPlanner::VelocityProfile TrapezoidPlanner::computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr) {
    VelocityProfile profile = {};

    float exit_speed    = sqrtf(exit_speed_sqr);
    float nominal_speed = computeNominalSpeed(block);

    // Compute intersection point where accel meets decel
    // Using trapezoidal profile equations
    float millimeters = block->millimeters;
    float accel       = block->acceleration;

    // Distance to accelerate from entry to nominal: d = (v² - v₀²) / (2a)
    float accel_dist = computeAccelDistance(entry_speed, nominal_speed, accel);

    // Distance to decelerate from nominal to exit: d = (v₀² - v²) / (2a)
    float decel_dist = computeDecelDistance(nominal_speed, exit_speed, accel);

    // Check if we have room for full profile
    float intersect_dist = accel_dist + decel_dist;

    if (intersect_dist > millimeters) {
        // Not enough distance - compute intersection of accel and decel curves
        // v² = entry² + 2*a*d_accel = exit² + 2*a*(mm - d_accel)
        // Solving: d_accel = (exit² - entry² + 2*a*mm) / (4*a)
        accel_dist = (exit_speed_sqr - entry_speed * entry_speed + 2.0f * accel * millimeters) / (4.0f * accel);

        if (accel_dist < 0.0f) {
            accel_dist = 0.0f;  // Pure deceleration
        } else if (accel_dist > millimeters) {
            accel_dist = millimeters;  // Pure acceleration
        }

        decel_dist = millimeters - accel_dist;

        // Compute peak speed at intersection
        float peak_speed_sqr  = entry_speed * entry_speed + 2.0f * accel * accel_dist;
        profile.maximum_speed = sqrtf(peak_speed_sqr);
    } else {
        profile.maximum_speed = nominal_speed;
    }

    // Set profile boundaries (measured from block END)
    profile.accelerate_until = millimeters - accel_dist;  // mm from end where accel phase ends
    profile.decelerate_after = decel_dist;                // mm from end where decel phase starts
    profile.exit_speed       = exit_speed;

    // Determine initial ramp type
    if (entry_speed < profile.maximum_speed - 0.0001f) {
        profile.initial_ramp_type = RAMP_ACCEL;
    } else if (profile.accelerate_until > profile.decelerate_after) {
        profile.initial_ramp_type = RAMP_CRUISE;
    } else {
        profile.initial_ramp_type = RAMP_DECEL;
    }

    return profile;
}

TrapezoidPlanner::RampUpdate TrapezoidPlanner::updateRamp(
    uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary) {
    RampUpdate update = {};

    // Note: current_accel is the block's acceleration rate, not current acceleration state
    // For trapezoidal profiles, acceleration is constant during accel/decel phases

    switch (ramp_type) {
        case RAMP_ACCEL: {
            // Constant acceleration: v = v₀ + a*t, d = v₀*t + 0.5*a*t²
            float speed_delta = current_accel * time_var;
            float dist        = time_var * (current_speed + 0.5f * speed_delta);

            update.speed_delta       = speed_delta;
            update.distance_traveled = dist;
            update.accel_delta       = 0.0f;  // Acceleration doesn't change in trapezoid

            float new_remaining = mm_remaining - dist;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_CRUISE;
            }
            break;
        }

        case RAMP_CRUISE: {
            // Constant velocity: d = v*t
            float dist = current_speed * time_var;

            update.speed_delta       = 0.0f;
            update.distance_traveled = dist;
            update.accel_delta       = 0.0f;

            float new_remaining = mm_remaining - dist;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_DECEL;
            }
            break;
        }

        case RAMP_DECEL:
        case RAMP_DECEL_OVERRIDE: {
            // Constant deceleration: v = v₀ - a*t, d = v₀*t - 0.5*a*t²
            float speed_delta = current_accel * time_var;

            if (current_speed > speed_delta) {
                float dist               = time_var * (current_speed - 0.5f * speed_delta);
                update.speed_delta       = -speed_delta;
                update.distance_traveled = dist;
            } else {
                // At or near zero speed
                update.speed_delta       = -current_speed;
                update.distance_traveled = mm_remaining;  // Finish the block
            }
            update.accel_delta = 0.0f;

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining <= phase_boundary || current_speed <= speed_delta) {
                update.phase_complete = true;
                // No next ramp type - block complete
            }
            break;
        }
    }

    return update;
}

// Register TrapezoidPlanner with the factory
namespace {
    PlannerFactory::InstanceBuilder<TrapezoidPlanner> registration("trapezoid_planner");
}
