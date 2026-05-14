// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2011 Jens Geisler
// Copyright (c) 2018 -	Bart Dring
// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  TrapezoidPlanner.h - Trapezoidal velocity profile planner
  
  This is the original GRBL-style planner with constant acceleration/deceleration,
  producing trapezoidal velocity profiles with three phases:
  - Acceleration (constant positive acceleration)
  - Cruise (constant velocity)
  - Deceleration (constant negative acceleration)
*/

#include "BasePlanner.h"

class TrapezoidPlanner : public BasePlanner {
public:
    TrapezoidPlanner(const char* name) : BasePlanner(name) {}

    // Ramp types for trapezoidal profiles
    static constexpr uint8_t RAMP_ACCEL          = 0;
    static constexpr uint8_t RAMP_CRUISE         = 1;
    static constexpr uint8_t RAMP_DECEL          = 2;
    static constexpr uint8_t RAMP_DECEL_OVERRIDE = 3;

    // Configuration (no extra parameters for trapezoid)
    void group(Configuration::HandlerBase& handler) override {}

    // Core planner algorithm - backward/forward pass
    void recalculate() override;

    // Trapezoidal kinematics: d = (v² - v₀²) / (2a)
    float computeAccelDistance(float v_entry, float v_exit, float accel) override {
        return (v_exit * v_exit - v_entry * v_entry) / (2.0f * accel);
    }

    float computeDecelDistance(float v_entry, float v_exit, float accel) override {
        return (v_entry * v_entry - v_exit * v_exit) / (2.0f * accel);
    }

    // Compute velocity profile for segment generation
    VelocityProfile computeVelocityProfile(plan_block_t* block, float entry_speed, float exit_speed_sqr) override;

    // Update ramp state during segment generation
    RampUpdate updateRamp(
        uint8_t ramp_type, float time_var, float current_speed, float current_accel, float mm_remaining, float phase_boundary) override;

protected:
    // Backward pass: maximize entry speeds from end to start
    void recalculateBackward();

    // Forward pass: limit speeds based on achievable acceleration
    void recalculateForward();
};
