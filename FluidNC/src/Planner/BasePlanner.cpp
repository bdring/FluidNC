// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2011 Jens Geisler
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "BasePlanner.h"
#include "../Planner.h"
#include "../Machine/MachineConfig.h"
#include "../Machine/Homing.h"
#include "../Protocol.h"
#include "../System.h"
#include "../NutsBolts.h"
#include "../Logging.h"

#include <cstring>
#include <cmath>

void BasePlanner::init() {
    if (_block_buffer) {
        delete[] _block_buffer;
    }
    _block_buffer = new plan_block_t[config->_planner_blocks];
}

void BasePlanner::reset() {
    memset(&_pl, 0, sizeof(PlannerState));
    resetBuffer();
}

void BasePlanner::resetBuffer() {
    _block_buffer_tail    = 0;
    _block_buffer_head    = 0;
    _next_buffer_head     = 1;
    _block_buffer_planned = 0;
}

// Returns the index of the next block in the ring buffer. Also called by stepper segment buffer.
uint8_t BasePlanner::nextBlockIndex(uint8_t idx) {
    idx++;
    if (idx == config->_planner_blocks) {
        idx = 0;
    }
    return idx;
}

// Returns the index of the previous block in the ring buffer
uint8_t BasePlanner::prevBlockIndex(uint8_t idx) {
    if (idx == 0) {
        idx = config->_planner_blocks;
    }
    idx--;
    return idx;
}

// Returns address of first planner block, if available. Called by various main program functions.
plan_block_t* BasePlanner::getCurrentBlock() {
    if (_block_buffer_head == _block_buffer_tail) {
        return nullptr;  // Buffer empty
    }
    return &_block_buffer[_block_buffer_tail];
}

// Returns address of planner buffer block used by system motions. Called by segment generator.
plan_block_t* BasePlanner::getSystemMotionBlock() {
    return &_block_buffer[_block_buffer_head];
}

// Called when the current block is no longer needed. Discards the block and makes the memory
// available for new blocks.
void BasePlanner::discardCurrentBlock() {
    if (_block_buffer_head != _block_buffer_tail) {  // Discard non-empty buffer.
        uint8_t block_index = nextBlockIndex(_block_buffer_tail);
        // Push block_buffer_planned pointer, if encountered.
        if (_block_buffer_tail == _block_buffer_planned) {
            _block_buffer_planned = block_index;
        }
        _block_buffer_tail = block_index;
    }
}

// Returns the availability status of the block ring buffer. True, if full.
bool BasePlanner::checkFullBuffer() {
    return _block_buffer_tail == _next_buffer_head;
}

// Returns the number of available blocks in the planner buffer.
// Called from report_realtime_status
uint8_t BasePlanner::getBlockBufferAvailable() {
    if (_block_buffer_head >= _block_buffer_tail) {
        return (config->_planner_blocks - 1) - (_block_buffer_head - _block_buffer_tail);
    } else {
        return _block_buffer_tail - _block_buffer_head - 1;
    }
}

// Called by step segment buffer when computing executing block velocity profile.
float BasePlanner::getExecBlockExitSpeedSqr() {
    uint8_t block_index = nextBlockIndex(_block_buffer_tail);
    if (block_index == _block_buffer_head) {
        return 0.0f;
    }
    return _block_buffer[block_index].entry_speed_sqr;
}

// Reset the planner position vectors. Called by the system abort/initialization routine.
void BasePlanner::syncPosition() {
    // TODO: For motor configurations not in the same coordinate frame as the machine position,
    // this function needs to be updated to accommodate the difference.
    if (config->_axes) {
        get_steps(_pl.position);
    }
}

// Re-initialize buffer plan with a partially completed block, assumed to exist at the buffer tail.
// Called after a steppers have come to a complete stop for a feed hold and the cycle is stopped.
void BasePlanner::cycleReinitialize() {
    // Re-plan from a complete stop. Reset planner entry speeds and buffer planned pointer.
    Stepper::update_plan_block_parameters();
    _block_buffer_planned = _block_buffer_tail;
    recalculate();
}

// Computes and returns block nominal speed based on running condition and override values.
// NOTE: All system motion commands, such as homing/parking, are not subject to overrides.
float BasePlanner::computeNominalSpeed(plan_block_t* block) {
    float nominal_speed = block->programmed_rate;
    if (block->motion.rapidMotion) {
        nominal_speed *= (0.01f * sys.r_override());
    } else {
        if (!(block->motion.noFeedOverride)) {
            nominal_speed *= (0.01f * sys.f_override());
        }
        if (nominal_speed > block->rapid_rate) {
            nominal_speed = block->rapid_rate;
        }
    }
    if (nominal_speed > MINIMUM_FEED_RATE) {
        return nominal_speed;
    }
    return MINIMUM_FEED_RATE;
}

// Computes and updates the max entry speed (sqr) of the block, based on the minimum of the junction's
// previous and current nominal speeds and max junction speed.
void BasePlanner::computeProfileParameters(plan_block_t* block, float nominal_speed, float prev_nominal_speed) {
    // Compute the junction maximum entry based on the minimum of the junction speed and neighboring nominal speeds.
    if (nominal_speed > prev_nominal_speed) {
        block->max_entry_speed_sqr = prev_nominal_speed * prev_nominal_speed;
    } else {
        block->max_entry_speed_sqr = nominal_speed * nominal_speed;
    }

    if (block->max_entry_speed_sqr > block->max_junction_speed_sqr) {
        block->max_entry_speed_sqr = block->max_junction_speed_sqr;
    }
}

// Re-calculates buffered motions profile parameters upon a motion-based override change.
void BasePlanner::updateVelocityProfileParameters() {
    uint8_t       block_index = _block_buffer_tail;
    plan_block_t* block;
    float         nominal_speed;
    float         prev_nominal_speed = SOME_LARGE_VALUE;  // Set high for first block nominal speed calculation.

    while (block_index != _block_buffer_head) {
        block         = &_block_buffer[block_index];
        nominal_speed = computeNominalSpeed(block);
        computeProfileParameters(block, nominal_speed, prev_nominal_speed);
        prev_nominal_speed = nominal_speed;
        block_index        = nextBlockIndex(block_index);
    }
    _pl.previous_nominal_speed = prev_nominal_speed;  // Update prev nominal speed for next incoming block.

    if (_block_buffer_tail != _block_buffer_head) {
        cycleReinitialize();
    }
}

void BasePlanner::getPlannerMpos(float* target) {
    auto n_axis = Axes::_numberAxis;
    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        target[axis] = steps_to_motor_pos(_pl.position[axis], axis);
    }
}

// Add a new linear movement to the buffer. target[MAX_N_AXIS] is the signed, absolute target position
// in millimeters. Feed rate specifies the speed of the motion. If feed rate is inverted, the feed
// rate is taken to mean "frequency" and would complete the operation in 1/feed_rate minutes.
// Returns true on success.
bool BasePlanner::bufferLine(float* target, plan_line_data_t* pl_data) {
    // Prepare and initialize new block. Copy relevant pl_data for block execution.
    plan_block_t* block = &_block_buffer[_block_buffer_head];
    memset(block, 0, sizeof(plan_block_t));  // Zero all block values.

    block->motion              = pl_data->motion;
    block->coolant             = pl_data->coolant;
    block->spindle             = pl_data->spindle;
    block->spindle_speed       = pl_data->spindle_speed;
    block->line_number         = pl_data->line_number;
    block->is_jog              = pl_data->is_jog;
    block->sync_mode           = pl_data->sync_mode;
    block->feed_per_revolution = pl_data->feed_per_revolution;

    // CSS (Constant Surface Speed) data
    block->css_mode           = pl_data->css_mode;
    block->css_surface_speed  = pl_data->css_surface_speed;
    block->css_max_rpm        = pl_data->css_max_rpm;
    block->css_start_position = 0.0f;

    // Compute and store initial move distance data
    steps_t position_steps[MAX_N_AXIS];
    if (block->motion.systemMotion) {
        get_steps(position_steps);
    } else {
        if (!block->is_jog && Homing::unhomed_axes()) {
            log_info("Unhomed axes: " << Axes::maskToNames(Homing::unhomed_axes()));
            send_alarm(ExecAlarm::Unhomed);
            return false;
        }
        copyAxes(position_steps, _pl.position);
    }

    // Set CSS start position for lathe CSS mode
    if (block->css_mode) {
        axis_t css_axis = config->_css_axis;
        if (css_axis != INVALID_AXIS && css_axis < MAX_N_AXIS) {
            block->css_start_position = steps_to_motor_pos(position_steps[css_axis], css_axis) - pl_data->css_tool_offset;
        }
    }

    steps_t target_steps[MAX_N_AXIS];
    float   unit_vec[MAX_N_AXIS];
    auto    n_axis = Axes::_numberAxis;

    for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
        // Calculate target position in absolute steps, number of steps for each axis, and determine max step events.
        // Also, compute individual axes distance for move and prep unit vector calculations.
        // NOTE: Computes true distance from converted step values.
        target_steps[axis]      = motor_pos_to_steps(target[axis], axis);
        block->steps[axis]      = labs(target_steps[axis] - position_steps[axis]);
        block->step_event_count = MAX(block->step_event_count, block->steps[axis]);
        float delta_mm          = steps_to_motor_pos((target_steps[axis] - position_steps[axis]), axis);
        unit_vec[axis]          = delta_mm;  // Store unit vector numerator
        // Set direction bits. Bit enabled always means direction is negative.
        if (delta_mm < 0.0) {
            block->direction_bits |= bitnum_to_mask(axis);
        }
    }

    // Bail if this is a zero-length block. Highly unlikely to occur.
    if (block->step_event_count == 0) {
        return false;
    }

    // Calculate the unit vector of the line move and the block maximum feed rate and acceleration scaled
    // down such that no individual axes maximum values are exceeded with respect to the line direction.
    // NOTE: This calculation assumes all axes are orthogonal (Cartesian) and works with ABC-axes,
    // if they are also orthogonal/independent. Operates on the absolute value of the unit vector.
    block->millimeters  = convert_delta_vector_to_unit_vector(unit_vec);
    block->acceleration = limit_acceleration_by_axis_maximum(unit_vec);
    block->rapid_rate   = limit_rate_by_axis_maximum(unit_vec);

    // Store programmed rate.
    if (block->motion.rapidMotion) {
        block->programmed_rate = block->rapid_rate;
    } else {
        block->programmed_rate = pl_data->feed_rate;
        if (block->motion.inverseTime) {
            block->programmed_rate *= block->millimeters;
        }
    }

    // TODO: Need to check this method handling zero junction speeds when starting from rest.
    if ((_block_buffer_head == _block_buffer_tail) || (block->motion.systemMotion)) {
        // Initialize block entry speed as zero. Assume it will be starting from rest. Planner will correct this later.
        // If system motion, the system motion block always is assumed to start from rest and end at a complete stop.
        block->entry_speed_sqr        = 0.0;
        block->max_junction_speed_sqr = 0.0;  // Starting from rest. Enforce start from zero velocity.
    } else {
        // Compute maximum allowable entry speed at junction by centripetal acceleration approximation.
        // Let a circle be tangent to both previous and current path line segments, where the junction
        // deviation is defined as the distance from the junction to the closest edge of the circle,
        // colinear with the circle center. The circular segment joining the two paths represents the
        // path of centripetal acceleration. Solve for max velocity based on max acceleration about the
        // radius of the circle, defined indirectly by junction deviation. This may be also viewed as
        // path width or max_jerk in the previous Grbl version. This approach does not actually deviate
        // from path, but used as a robust way to compute cornering speeds, as it takes into account the
        // nonlinearities of both the junction angle and junction velocity.
        //
        // NOTE: If the junction deviation value is finite, the motions are executed in exact path
        // mode (G61). If the junction deviation value is zero, the motions are executed in exact
        // stop mode (G61.1) manner. In the future, if continuous mode (G64) is desired, the math here
        // is exactly the same. Instead of motioning all the way to junction point, the machine will
        // just follow the arc circle defined here. The Arduino doesn't have the CPU cycles to perform
        // a continuous mode path, but ARM-based microcontrollers most certainly do.
        //
        // NOTE: The max junction speed is a fixed value, since machine acceleration limits cannot be
        // changed dynamically during operation nor can the line move geometry. This must be kept in
        // memory in the event of a feedrate override changing the nominal speeds of blocks, which can
        // change the overall maximum entry speed conditions of all blocks.
        float junction_unit_vec[MAX_N_AXIS];
        float junction_cos_theta = 0.0;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
            junction_cos_theta -= _pl.previous_unit_vec[axis] * unit_vec[axis];
            junction_unit_vec[axis] = unit_vec[axis] - _pl.previous_unit_vec[axis];
        }

        // NOTE: Computed without any expensive trig, sin() or acos(), by trig half angle identity of cos(theta).
        if (junction_cos_theta > 0.999999f) {
            // For a 0 degree acute junction, just set minimum junction speed.
            block->max_junction_speed_sqr = MINIMUM_JUNCTION_SPEED * MINIMUM_JUNCTION_SPEED;
        } else if (junction_cos_theta < -0.999999f) {
            // Junction is a straight line or 180 degrees. Junction speed is infinite.
            block->max_junction_speed_sqr = SOME_LARGE_VALUE;
        } else {
            convert_delta_vector_to_unit_vector(junction_unit_vec);
            float junction_acceleration   = limit_acceleration_by_axis_maximum(junction_unit_vec);
            float sin_theta_d2            = sqrtf(0.5f * (1.0f - junction_cos_theta));  // Trig half angle identity. Always positive.
            block->max_junction_speed_sqr = MAX(MINIMUM_JUNCTION_SPEED * MINIMUM_JUNCTION_SPEED,
                                                (junction_acceleration * config->_junctionDeviation * sin_theta_d2) / (1.0f - sin_theta_d2));
        }
    }

    // Block system motion from updating this data to ensure next g-code motion is computed correctly.
    if (!(block->motion.systemMotion)) {
        float nominal_speed = computeNominalSpeed(block);
        computeProfileParameters(block, nominal_speed, _pl.previous_nominal_speed);
        _pl.previous_nominal_speed = nominal_speed;

        // Update previous path unit_vector and planner position.
        copyAxes(_pl.previous_unit_vec, unit_vec);
        copyAxes(_pl.position, target_steps);

        // New block is all set. Update buffer head and next buffer head indices.
        _block_buffer_head = _next_buffer_head;
        _next_buffer_head  = nextBlockIndex(_block_buffer_head);

        // Finish up by recalculating the plan with the new block.
        recalculate();
    }

    return true;
}

// Default implementations for kinematic calculations (trapezoidal)
float BasePlanner::computeAccelDistance(float v_entry, float v_exit, float accel) {
    // d = (v² - v₀²) / (2a)
    return (v_exit * v_exit - v_entry * v_entry) / (2.0f * accel);
}

float BasePlanner::computeDecelDistance(float v_entry, float v_exit, float accel) {
    // d = (v₀² - v²) / (2a)
    return (v_entry * v_entry - v_exit * v_exit) / (2.0f * accel);
}

// Default velocity profile computation (will be overridden)
BasePlanner::VelocityProfile BasePlanner::computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr) {
    VelocityProfile profile = {};
    // Default implementation - derived classes should override
    return profile;
}

// Default ramp update (will be overridden)
BasePlanner::RampUpdate BasePlanner::updateRamp(
    uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary) {
    RampUpdate update = {};
    // Default implementation - derived classes should override
    return update;
}
