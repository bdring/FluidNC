// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  SCurvePlanner.h - Jerk-limited S-curve velocity profile planner
  
  Extends TrapezoidPlanner with smooth acceleration profiles that limit jerk.
  Instead of 3 phases (accel, cruise, decel), uses 7 phases:
  
    Phase 0: Jerk+ (acceleration increasing from 0 to a_max)
    Phase 1: Const Accel (acceleration constant at a_max)
    Phase 2: Jerk- (acceleration decreasing from a_max to 0)
    Phase 3: Cruise (constant velocity)
    Phase 4: Jerk- (acceleration decreasing from 0 to -a_max)
    Phase 5: Const Decel (acceleration constant at -a_max)
    Phase 6: Jerk+ (acceleration increasing from -a_max to 0)
  
  This provides smoother motion, reduced mechanical wear, and less vibration
  at the cost of slightly longer acceleration distances.
*/

#include "TrapezoidPlanner.h"
#include "SCurveMath.h"

class SCurvePlanner : public TrapezoidPlanner {
public:
    SCurvePlanner(const char* name) : TrapezoidPlanner(name) {}

    // Extended ramp types for 7-phase profile
    static constexpr uint8_t RAMP_JERK_ACCEL_UP   = 0;  // Jerk+ : accel 0 → a_max
    static constexpr uint8_t RAMP_CONST_ACCEL     = 1;  // Constant acceleration at a_max
    static constexpr uint8_t RAMP_JERK_ACCEL_DOWN = 2;  // Jerk- : accel a_max → 0
    static constexpr uint8_t RAMP_CRUISE          = 3;  // Constant velocity
    static constexpr uint8_t RAMP_JERK_DECEL_UP   = 4;  // Jerk- : accel 0 → -a_max
    static constexpr uint8_t RAMP_CONST_DECEL     = 5;  // Constant deceleration at -a_max
    static constexpr uint8_t RAMP_JERK_DECEL_DOWN = 6;  // Jerk+ : accel -a_max → 0
    static constexpr uint8_t RAMP_DECEL_OVERRIDE  = 7;  // Emergency stop (falls back to trapezoidal)

    // Configuration - jerk limit
    void group(Configuration::HandlerBase& handler) override {
        handler.item("jerk", _jerk, 1000.0f, 1000000.0f);  // mm/min³
    }

    void init() override;

    // Override recalculate with continuous jerk algorithm
    void recalculate() override;

    // S-curve distance calculations (override trapezoidal)
    float computeAccelDistance(float v_entry, float v_exit, float accel) override;
    float computeDecelDistance(float v_entry, float v_exit, float accel) override;

    // Extended velocity profile with 7 phase boundaries
    VelocityProfile computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr) override;

    // 7-phase ramp update for segment generation
    RampUpdate updateRamp(
        uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary) override;

    // Set jerk limit (can be called during configuration)
    void  setJerkLimit(float jerk) { _jerk = jerk; }
    float getJerkLimit() const { return _jerk; }

    // Feed hold handling
    // Computes jerk-limited deceleration profile for feed hold
    // Returns distance needed to stop smoothly
    float computeFeedHoldDistance(float current_speed, float current_accel, float a_max);

    // Check if we should use S-curve or trapezoidal for this stop
    // Returns true if S-curve stop is feasible
    bool canUseSCurveStop(float current_speed, float distance_remaining);

protected:
    // Jerk limit in mm/min³ (configured per-axis, this is the limiting value)
    float _jerk = 50000.0f;

    // Override backward/forward passes for continuous jerk
    void recalculateBackward();
    void recalculateForward();

    // Compute junction acceleration limit based on direction change
    float computeJunctionAccelLimit(float* prev_unit_vec, float* unit_vec);

    // Helper to compute S-curve distance with given entry/exit accelerations
    float computeDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, bool accelerating);
};
