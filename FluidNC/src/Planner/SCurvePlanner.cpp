// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SCurvePlanner.h"
#include "../Planner.h"
#include "../Stepper.h"
#include "../Machine/MachineConfig.h"
#include "../NutsBolts.h"

#include <cmath>
#include <algorithm>

void SCurvePlanner::init() {
    TrapezoidPlanner::init();

    // Jerk limit is set by plan_init() from Stepping config after construction.
    // Default value is used if not explicitly configured.
    // _jerk is already initialized to 50000.0f in the class definition.
}

void SCurvePlanner::recalculate() {
    recalculateBackward();
    recalculateForward();
}

void SCurvePlanner::recalculateBackward() {
    if (_block_buffer_head == _block_buffer_tail) {
        return;  // Buffer empty
    }

    uint8_t block_index = prevBlockIndex(_block_buffer_head);
    if (block_index == _block_buffer_planned) {
        return;  // Only one plannable block
    }

    plan_block_t* current = &_block_buffer[block_index];

    // Last block ends at rest (exit_speed = 0, exit_accel = 0)
    float exit_speed = 0.0f;
    float exit_accel = 0.0f;

    // Compute max entry speed using S-curve deceleration distance
    float max_entry_speed = SCurveMath::solveMaxEntrySpeed(current->millimeters, exit_speed, exit_accel, current->acceleration, _jerk);

    current->entry_speed_sqr = std::min(current->max_entry_speed_sqr, max_entry_speed * max_entry_speed);

    // Compute entry acceleration for smooth jerk (simplified: use 0)
    current->entry_accel = SCurveMath::solveEntryAccel(
        current->millimeters, sqrtf(current->entry_speed_sqr), exit_speed, exit_accel, current->acceleration, _jerk);

    // Clamp to max allowable
    current->entry_accel = SCurveMath::clamp(current->entry_accel, -current->max_entry_accel, current->max_entry_accel);

    block_index = prevBlockIndex(block_index);

    if (block_index == _block_buffer_planned) {
        if (block_index == _block_buffer_tail) {
            Stepper::update_plan_block_parameters();
        }
        return;
    }

    // Process remaining blocks
    plan_block_t* next;
    while (block_index != _block_buffer_planned) {
        next        = current;
        current     = &_block_buffer[block_index];
        block_index = prevBlockIndex(block_index);

        if (block_index == _block_buffer_tail) {
            Stepper::update_plan_block_parameters();
        }

        // Exit conditions of current = entry conditions of next
        exit_speed = sqrtf(next->entry_speed_sqr);
        exit_accel = next->entry_accel;

        // Compute max entry speed using S-curve decel distance
        if (current->entry_speed_sqr != current->max_entry_speed_sqr) {
            float max_entry = SCurveMath::solveMaxEntrySpeed(current->millimeters, exit_speed, exit_accel, current->acceleration, _jerk);

            current->entry_speed_sqr = std::min(current->max_entry_speed_sqr, max_entry * max_entry);
        }

        // Compute entry accel for smooth transition
        current->entry_accel = SCurveMath::solveEntryAccel(
            current->millimeters, sqrtf(current->entry_speed_sqr), exit_speed, exit_accel, current->acceleration, _jerk);
        current->entry_accel = SCurveMath::clamp(current->entry_accel, -current->max_entry_accel, current->max_entry_accel);
    }
}

void SCurvePlanner::recalculateForward() {
    if (_block_buffer_head == _block_buffer_tail) {
        return;
    }

    plan_block_t* next        = &_block_buffer[_block_buffer_planned];
    uint8_t       block_index = nextBlockIndex(_block_buffer_planned);

    while (block_index != _block_buffer_head) {
        plan_block_t* current = next;
        next                  = &_block_buffer[block_index];

        // Check if we can achieve the planned next entry speed
        if (current->entry_speed_sqr < next->entry_speed_sqr) {
            float achievable_speed = SCurveMath::solveMaxExitSpeed(
                current->millimeters, sqrtf(current->entry_speed_sqr), current->entry_accel, current->acceleration, _jerk);

            if (achievable_speed * achievable_speed < next->entry_speed_sqr) {
                // Can't reach planned speed - reduce it
                next->entry_speed_sqr = achievable_speed * achievable_speed;

                // Recompute entry accel for new speed
                next->entry_accel = SCurveMath::solveEntryAccel(
                    current->millimeters, sqrtf(current->entry_speed_sqr), achievable_speed, next->entry_accel, current->acceleration, _jerk);

                _block_buffer_planned = block_index;
            }
        }

        // Block at max entry speed creates optimal breakpoint
        if (next->entry_speed_sqr == next->max_entry_speed_sqr) {
            _block_buffer_planned = block_index;
        }

        block_index = nextBlockIndex(block_index);
    }
}

float SCurvePlanner::computeAccelDistance(float v_entry, float v_exit, float accel) {
    return SCurveMath::accelDistance(v_entry, v_exit, accel, _jerk);
}

float SCurvePlanner::computeDecelDistance(float v_entry, float v_exit, float accel) {
    return SCurveMath::decelDistance(v_entry, v_exit, accel, _jerk);
}

float SCurvePlanner::computeDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, bool accelerating) {
    if (accelerating) {
        return SCurveMath::accelDistanceWithAccel(v_entry, a_entry, v_exit, a_exit, a_max, _jerk);
    } else {
        return SCurveMath::decelDistanceWithAccel(v_entry, a_entry, v_exit, a_exit, a_max, _jerk);
    }
}

float SCurvePlanner::computeJunctionAccelLimit(float* prev_unit_vec, float* unit_vec) {
    // Compute junction angle
    float junction_cos_theta = 0.0f;
    auto  n_axis             = Axes::_numberAxis;

    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        junction_cos_theta -= prev_unit_vec[axis] * unit_vec[axis];
    }

    // For sharp corners, limit acceleration more
    // For straight lines, allow full acceleration
    if (junction_cos_theta > 0.999f) {
        // Nearly 180° reversal - limit acceleration significantly
        return 0.0f;
    } else if (junction_cos_theta < -0.999f) {
        // Straight line - no limit needed
        return SOME_LARGE_VALUE;
    }

    // Scale acceleration limit based on angle
    // Gentler corners allow higher acceleration
    float angle_factor = (1.0f - junction_cos_theta) / 2.0f;  // 0 for straight, 1 for reversal
    return (1.0f - angle_factor) * SOME_LARGE_VALUE;
}

SCurvePlanner::VelocityProfile SCurvePlanner::computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr) {
    VelocityProfile profile = {};

    float exit_speed    = sqrtf(exit_speed_sqr);
    float nominal_speed = computeNominalSpeed(block);
    float millimeters   = block->millimeters;
    float accel         = block->acceleration;

    // Get entry/exit accelerations from block (for continuous jerk)
    float entry_accel = block->entry_accel;
    float exit_accel  = 0.0f;  // Last block exits at 0 accel

    // Compute peak velocity achievable in this distance
    float v_peak = SCurveMath::computePeakVelocity(millimeters, entry_speed, exit_speed, nominal_speed, accel, _jerk);

    profile.maximum_speed = v_peak;
    profile.exit_speed    = exit_speed;
    profile.entry_accel   = entry_accel;
    profile.jerk          = _jerk;

    // Plan full 7-phase profile
    SCurveMath::SCurveProfile scurve =
        SCurveMath::planProfile(millimeters, entry_speed, entry_accel, exit_speed, exit_accel, nominal_speed, accel, _jerk);

    // Convert phase distances to "mm from block end" format
    // Phase boundaries are measured from the END of the block
    float d_remaining = millimeters;

    for (int i = 0; i < 7; i++) {
        d_remaining -= scurve.d[i];
        profile.phase_end[i] = d_remaining;
    }

    // Determine initial ramp type based on which phase we start in
    if (scurve.t[0] > 0.0001f) {
        profile.initial_ramp_type = RAMP_JERK_ACCEL_UP;
    } else if (scurve.t[1] > 0.0001f) {
        profile.initial_ramp_type = RAMP_CONST_ACCEL;
    } else if (scurve.t[2] > 0.0001f) {
        profile.initial_ramp_type = RAMP_JERK_ACCEL_DOWN;
    } else if (scurve.t[3] > 0.0001f) {
        profile.initial_ramp_type = RAMP_CRUISE;
    } else if (scurve.t[4] > 0.0001f) {
        profile.initial_ramp_type = RAMP_JERK_DECEL_UP;
    } else if (scurve.t[5] > 0.0001f) {
        profile.initial_ramp_type = RAMP_CONST_DECEL;
    } else {
        profile.initial_ramp_type = RAMP_JERK_DECEL_DOWN;
    }

    // Set traditional boundaries for compatibility
    // accelerate_until = end of accel phases (after phase 2)
    // decelerate_after = start of decel phases (before phase 4)
    profile.accelerate_until = profile.phase_end[2];
    profile.decelerate_after = profile.phase_end[3];

    return profile;
}

SCurvePlanner::RampUpdate SCurvePlanner::updateRamp(
    uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary) {
    RampUpdate update = {};

    switch (ramp_type) {
        case RAMP_JERK_ACCEL_UP: {
            // Acceleration increasing: a(t) = a₀ + j*t
            // v(t) = v₀ + a₀*t + ½*j*t²
            // s(t) = v₀*t + ½*a₀*t² + ⅙*j*t³

            update.accel_delta       = _jerk * time_var;
            update.speed_delta       = SCurveMath::jerkVelocityChange(current_accel, _jerk, time_var);
            update.distance_traveled = SCurveMath::jerkDistance(current_speed, current_accel, _jerk, time_var);

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_CONST_ACCEL;
            }
            break;
        }

        case RAMP_CONST_ACCEL: {
            // Constant acceleration (same as trapezoidal)
            // a(t) = a_max
            // v(t) = v₀ + a_max*t
            // s(t) = v₀*t + ½*a_max*t²

            update.accel_delta       = 0.0f;
            update.speed_delta       = current_accel * time_var;
            update.distance_traveled = time_var * (current_speed + 0.5f * update.speed_delta);

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_JERK_ACCEL_DOWN;
            }
            break;
        }

        case RAMP_JERK_ACCEL_DOWN: {
            // Acceleration decreasing toward 0: a(t) = a₀ - j*t

            update.accel_delta       = -_jerk * time_var;
            update.speed_delta       = SCurveMath::jerkVelocityChange(current_accel, -_jerk, time_var);
            update.distance_traveled = SCurveMath::jerkDistance(current_speed, current_accel, -_jerk, time_var);

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_CRUISE;
            }
            break;
        }

        case RAMP_CRUISE: {
            // Constant velocity: a = 0

            update.accel_delta       = 0.0f;
            update.speed_delta       = 0.0f;
            update.distance_traveled = current_speed * time_var;

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_JERK_DECEL_UP;
            }
            break;
        }

        case RAMP_JERK_DECEL_UP: {
            // Acceleration decreasing (becoming negative): a(t) = a₀ - j*t

            update.accel_delta       = -_jerk * time_var;
            update.speed_delta       = SCurveMath::jerkVelocityChange(current_accel, -_jerk, time_var);
            update.distance_traveled = SCurveMath::jerkDistance(current_speed, current_accel, -_jerk, time_var);

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_CONST_DECEL;
            }
            break;
        }

        case RAMP_CONST_DECEL: {
            // Constant deceleration: a = -a_max

            update.accel_delta = 0.0f;
            float decel_amount = -current_accel * time_var;  // current_accel is negative

            if (current_speed > decel_amount) {
                update.speed_delta       = -decel_amount;  // Negative (slowing down)
                update.distance_traveled = time_var * (current_speed - 0.5f * decel_amount);
            } else {
                update.speed_delta       = -current_speed;
                update.distance_traveled = mm_remaining;
            }

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining < phase_boundary) {
                update.phase_complete = true;
                update.next_ramp_type = RAMP_JERK_DECEL_DOWN;
            }
            break;
        }

        case RAMP_JERK_DECEL_DOWN: {
            // Acceleration increasing toward 0: a(t) = a₀ + j*t (a₀ is negative)

            update.accel_delta       = _jerk * time_var;
            update.speed_delta       = SCurveMath::jerkVelocityChange(current_accel, _jerk, time_var);
            update.distance_traveled = SCurveMath::jerkDistance(current_speed, current_accel, _jerk, time_var);

            float new_remaining = mm_remaining - update.distance_traveled;
            if (new_remaining <= phase_boundary || current_speed + update.speed_delta <= 0.001f) {
                update.phase_complete = true;
                // Block complete - no next ramp type
            }
            break;
        }

        case RAMP_DECEL_OVERRIDE: {
            // Feed hold / emergency stop handling
            // Try to use S-curve deceleration if we have enough distance
            // Otherwise fall back to trapezoidal for quickest stop

            if (canUseSCurveStop(current_speed, mm_remaining)) {
                // Use S-curve deceleration for smooth stop
                // Start with jerk- phase (building negative acceleration)
                update.accel_delta       = -_jerk * time_var;
                update.speed_delta       = SCurveMath::jerkVelocityChange(current_accel, -_jerk, time_var);
                update.distance_traveled = SCurveMath::jerkDistance(current_speed, current_accel, -_jerk, time_var);

                // Check if we've reached full deceleration or need to finish
                if (current_speed + update.speed_delta <= 0.001f) {
                    update.speed_delta    = -current_speed;
                    update.phase_complete = true;
                }
            } else {
                // Emergency - use trapezoidal for fastest stop
                return TrapezoidPlanner::updateRamp(
                    TrapezoidPlanner::RAMP_DECEL_OVERRIDE, time_var, current_speed, current_accel, mm_remaining, phase_boundary);
            }
            break;
        }
    }

    return update;
}

float SCurvePlanner::computeFeedHoldDistance(float current_speed, float current_accel, float a_max) {
    // Compute distance needed to stop from current_speed with S-curve deceleration
    // Starting from current_accel (which might be non-zero if we're mid-acceleration)

    if (current_speed <= 0.001f) {
        return 0.0f;  // Already stopped
    }

    // Use S-curve decel distance calculation
    return SCurveMath::decelDistanceWithAccel(current_speed, current_accel, 0.0f, 0.0f, a_max, _jerk);
}

bool SCurvePlanner::canUseSCurveStop(float current_speed, float distance_remaining) {
    // Check if we have enough distance for a smooth S-curve stop
    // We need more distance for S-curve than trapezoidal, so use a safety margin

    if (current_speed <= 0.001f) {
        return true;  // Already stopped
    }

    // Estimate stopping distance using default max acceleration
    // This is a conservative estimate
    float default_accel   = 1000.0f * 60.0f;  // Assume 1000 mm/s² as reasonable max
    float scurve_distance = SCurveMath::decelDistance(current_speed, 0.0f, default_accel, _jerk);

    // Use S-curve if we have at least 1.2x the required distance (safety margin)
    return distance_remaining >= scurve_distance * 1.2f;
}

// Register SCurvePlanner with the factory
namespace {
    PlannerFactory::InstanceBuilder<SCurvePlanner> registration("scurve_planner");
}
