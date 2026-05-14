// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2024 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Planner.h - buffers movement commands and manages the acceleration profile plan
  
  This file provides the public interface for motion planning. The actual implementation
  is in the Planner/ subdirectory with polymorphic planners:
  - TrapezoidPlanner: Original GRBL-style trapezoidal profiles
  - SCurvePlanner: Jerk-limited S-curve profiles for smoother motion
*/

#include "Config.h"            // MAX_N_AXIS
#include "SpindleDatatypes.h"  // SpindleState
#include "GCode.h"             // CoolantState

#include <cstdint>

// Spindle synchronization modes
enum class SpindleSyncMode : uint8_t {
    None = 0,   // Normal time-based motion (G94)
    PerRev = 1, // Feed per revolution - soft sync, allows overrides (G95)
    Rigid = 2,  // Rigid threading - hard sync, no overrides (G33)
};

// Define planner data condition flags. Used to denote running conditions of a block.
struct PlMotion {
    uint8_t rapidMotion : 1;
    uint8_t systemMotion : 1;    // Single motion. Circumvents planner state. Used by home/park.
    uint8_t noFeedOverride : 1;  // Motion does not honor feed override.
    uint8_t inverseTime : 1;     // Interprets feed rate value as inverse time when set.
};

// This struct stores a linear movement of a g-code block motion with its critical "nominal" values
// are as specified in the source g-code.
struct plan_block_t {
    // Fields used by the bresenham algorithm for tracing the line
    // NOTE: Used by stepper algorithm to execute the block correctly. Do not alter these values.

    uint32_t steps[MAX_N_AXIS];  // Step count along each axis
    uint32_t step_event_count;   // The maximum step axis count and number of steps required to complete this block.
    AxisMask direction_bits;     // The direction bit set for this block (refers to *_DIRECTION_BIT in config.h)

    // Block condition data to ensure correct execution depending on states and overrides.
    PlMotion     motion;       // Block bitflag motion conditions. Copied from pl_line_data.
    SpindleState spindle;      // Spindle enable state
    CoolantState coolant;      // Coolant state
    int32_t      line_number;  // Block line number for real-time reporting. Copied from pl_line_data.

    // Fields used by the motion planner to manage acceleration. Some of these values may be updated
    // by the stepper module during execution of special motion cases for replanning purposes.
    float entry_speed_sqr;      // The current planned entry speed at block junction in (mm/min)^2
    float max_entry_speed_sqr;  // Maximum allowable entry speed based on the minimum of junction limit and
    //   neighboring nominal speeds with overrides in (mm/min)^2
    float acceleration;  // Axis-limit adjusted line acceleration in (mm/min^2). Does not change.
    float millimeters;   // The remaining distance for this block to be executed in (mm).
    // NOTE: This value may be altered by stepper algorithm during execution.

    // Stored rate limiting data used by planner when changes occur.
    float max_junction_speed_sqr;  // Junction entry speed limit based on direction vectors in (mm/min)^2
    float rapid_rate;              // Axis-limit adjusted maximum rate for this block direction in (mm/min)
    float programmed_rate;         // Programmed rate of this block (mm/min).

    // Stored spindle speed data used by spindle overrides and resuming methods.
    SpindleSpeed spindle_speed;  // Block spindle speed. Copied from pl_line_data.

    // Spindle synchronization data for G95/G33 moves
    SpindleSyncMode sync_mode;           // Synchronization mode
    float           feed_per_revolution; // Feed rate in mm/rev (for G95/G33)

    // CSS (Constant Surface Speed) data
    bool  css_mode;           // True when in G96 CSS mode
    float css_surface_speed;  // Surface speed in m/min (G96 S value)
    float css_max_rpm;        // Maximum RPM limit
    float css_start_position; // CSS axis position at block start (mm)

    bool is_jog;
    
    // S-curve planner extensions (used by SCurvePlanner, ignored by TrapezoidPlanner)
    float entry_accel;          // Entry acceleration at junction (mm/min²)
    float exit_accel;           // Exit acceleration at junction (mm/min²)
    float max_entry_accel;      // Max allowable entry accel based on direction change
    float jerk;                 // Axis-limited jerk for this block (mm/min³)
};

// Planner data prototype. Must be used when passing new motions to the planner.
struct plan_line_data_t {
    float        feed_rate;       // Desired feed rate for line motion. Value is ignored, if rapid motion.
    SpindleSpeed spindle_speed;   // Desired spindle speed through line motion.
    PlMotion     motion;          // Bitflag variable to indicate motion conditions. See defines above.
    SpindleState spindle;         // Spindle enable state
    CoolantState coolant;         // Coolant state
    int32_t      line_number;     // Desired line number to report when executing.
    bool         is_jog;          // true if this was generated due to a jog command
    bool         limits_checked;  // true if soft limits already checked
    
    // Spindle synchronization data
    SpindleSyncMode sync_mode;           // Synchronization mode (None/PerRev/Rigid)
    float           feed_per_revolution; // Feed rate in mm/rev (for G95/G33)

    // CSS (Constant Surface Speed) data
    bool  css_mode;           // True when in G96 CSS mode
    float css_surface_speed;  // Surface speed in m/min (G96 S value)
    float css_max_rpm;        // Maximum RPM limit
    float css_tool_offset;    // Tool length offset on CSS axis (added to machine position)
};

void plan_init();

// Initialize and reset the motion plan subsystem
void plan_reset();         // Reset all
void plan_reset_buffer();  // Reset buffer only.

// Add a new linear movement to the buffer. target[MAX_N_AXIS] is the signed, absolute target position
// in millimeters. Feed rate specifies the speed of the motion. If feed rate is inverted, the feed
// rate is taken to mean "frequency" and would complete the operation in 1/feed_rate minutes.
// Returns true on success.
bool plan_buffer_line(float* target, plan_line_data_t* pl_data);

// Called when the current block is no longer needed. Discards the block and makes the memory
// available for new blocks.
void plan_discard_current_block();

// Gets the planner block for the special system motion cases. (Parking/Homing)
plan_block_t* plan_get_system_motion_block();

// Gets the current block. Returns NULL if buffer empty
plan_block_t* plan_get_current_block();

// Called by step segment buffer when computing executing block velocity profile.
float plan_get_exec_block_exit_speed_sqr();

// Called by main program during planner calculations and step segment buffer during initialization.
float plan_compute_profile_nominal_speed(plan_block_t* block);

// Re-calculates buffered motions profile parameters upon a motion-based override change.
void plan_update_velocity_profile_parameters();

// Reset the planner position vector (in steps)
void plan_sync_position();

// Reinitialize plan with a partially completed block
void plan_cycle_reinitialize();

// Returns the number of available blocks are in the planner buffer.
uint8_t plan_get_block_buffer_available();

// Returns the status of the block ring buffer. True, if buffer is full.
uint8_t plan_check_full_buffer();

void plan_get_planner_mpos(float* target);
