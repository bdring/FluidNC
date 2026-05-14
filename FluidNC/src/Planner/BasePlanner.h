// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  BasePlanner.h - Abstract base class for motion planners
  
  This provides a polymorphic interface for different planner implementations:
  - TrapezoidPlanner: Original GRBL-style trapezoidal velocity profiles
  - SCurvePlanner: Jerk-limited S-curve velocity profiles
  
  Planners are configured via the YAML config file:
    SCurvePlanner:
      jerk: 50000       # mm/min³ (only for SCurve)
*/

#include "../Config.h"
#include "../Planner.h"
#include "../System.h"  // For steps_t
#include "../Configuration/Configurable.h"
#include "../Configuration/GenericFactory.h"

#include <cstdint>

class BasePlanner : public Configuration::Configurable {
public:
    BasePlanner(const char* name) : _name(name) {}
    virtual ~BasePlanner() = default;

    BasePlanner(const BasePlanner&)            = delete;
    BasePlanner(BasePlanner&&)                 = delete;
    BasePlanner& operator=(const BasePlanner&) = delete;
    BasePlanner& operator=(BasePlanner&&)      = delete;

    const char* name() const { return _name; }

    // Lifecycle
    virtual void init();
    virtual void reset();
    virtual void resetBuffer();

    // Block buffer management
    virtual bool  bufferLine(float* target, plan_line_data_t* pl_data);
    plan_block_t* getCurrentBlock();
    plan_block_t* getSystemMotionBlock();
    void          discardCurrentBlock();
    bool          checkFullBuffer();
    uint8_t       getBlockBufferAvailable();
    float         getExecBlockExitSpeedSqr();
    void          syncPosition();
    void          cycleReinitialize();
    void          updateVelocityProfileParameters();
    void          getPlannerMpos(float* target);

    // Compute nominal speed with overrides
    float computeNominalSpeed(plan_block_t* block);

    // Virtual methods for planner algorithm (override in derived classes)
    virtual void recalculate() = 0;

    // Kinematic calculations - key extension points for S-curve
    // These compute distance required for acceleration/deceleration
    virtual float computeAccelDistance(float v_entry, float v_exit, float accel);
    virtual float computeDecelDistance(float v_entry, float v_exit, float accel);

    // Velocity profile for segment generation
    struct VelocityProfile {
        float   accelerate_until;  // mm from block end where accel ends
        float   decelerate_after;  // mm from block end where decel starts
        float   maximum_speed;     // Peak velocity in this block
        float   exit_speed;        // Exit velocity
        uint8_t initial_ramp_type;

        // S-curve extensions (used by SCurvePlanner, ignored by TrapezoidPlanner)
        float entry_accel;   // Entry acceleration
        float phase_end[7];  // End position for each of 7 phases
        float jerk;          // Jerk rate for this block
    };

    virtual VelocityProfile computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr);

    // Segment generation - called by Stepper::prep_buffer()
    struct RampUpdate {
        float   speed_delta;        // Change in velocity
        float   accel_delta;        // Change in acceleration (for S-curve)
        float   distance_traveled;  // Distance covered in this update
        uint8_t next_ramp_type;     // Next ramp state
        bool    phase_complete;     // True if transitioning to next phase
    };

    virtual RampUpdate updateRamp(
        uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary);

protected:
    const char* _name;

    plan_block_t* _block_buffer         = nullptr;
    uint8_t       _block_buffer_tail    = 0;
    uint8_t       _block_buffer_head    = 0;
    uint8_t       _next_buffer_head     = 1;
    uint8_t       _block_buffer_planned = 0;

    struct PlannerState {
        steps_t position[MAX_N_AXIS];           // Planner position in steps
        float   previous_unit_vec[MAX_N_AXIS];  // Unit vector of previous segment
        float   previous_nominal_speed;         // Nominal speed of previous segment
    } _pl;

    // Helper methods
    uint8_t nextBlockIndex(uint8_t idx);
    uint8_t prevBlockIndex(uint8_t idx);
    void    computeProfileParameters(plan_block_t* block, float nominal_speed, float prev_nominal_speed);
};

// Factory for creating planners from configuration
using PlannerFactory = Configuration::GenericFactory<BasePlanner>;

// Global planner instance accessor
BasePlanner* getPlanner();
