// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  SCurveMath.h - Kinematic calculations for jerk-limited S-curve motion profiles
  
  S-curve profiles provide smooth acceleration by limiting jerk (the derivative of acceleration).
  Instead of instant acceleration changes (trapezoidal), acceleration ramps up and down smoothly.
  
  A full S-curve acceleration has 3 phases:
  1. Jerk+ : Acceleration increases from 0 to a_max
  2. Const : Acceleration stays at a_max  
  3. Jerk- : Acceleration decreases from a_max to 0
  
  Similarly for deceleration (with negative acceleration).
  
  Key equations during jerk phases:
    j(t) = J (constant jerk)
    a(t) = a₀ + J*t
    v(t) = v₀ + a₀*t + ½*J*t²
    s(t) = s₀ + v₀*t + ½*a₀*t² + ⅙*J*t³
*/

#include <cmath>

namespace SCurveMath {

    // ============================================================================
    // Core kinematic functions for jerk phases
    // ============================================================================

    // Distance traveled during jerk phase: s = v₀t + ½a₀t² + ⅙jt³
    inline float jerkDistance(float v0, float a0, float jerk, float t) {
        return v0 * t + 0.5f * a0 * t * t + (1.0f / 6.0f) * jerk * t * t * t;
    }

    // Velocity after jerk phase: v = v₀ + a₀t + ½jt²
    inline float jerkVelocity(float v0, float a0, float jerk, float t) {
        return v0 + a0 * t + 0.5f * jerk * t * t;
    }

    // Acceleration after jerk phase: a = a₀ + jt
    inline float jerkAccel(float a0, float jerk, float t) {
        return a0 + jerk * t;
    }

    // Time to change acceleration by delta_a at given jerk: t = |Δa| / j
    inline float jerkTime(float delta_a, float jerk) {
        return fabsf(delta_a / jerk);
    }

    // Velocity change during jerk phase of duration t: Δv = a₀t + ½jt²
    inline float jerkVelocityChange(float a0, float jerk, float t) {
        return a0 * t + 0.5f * jerk * t * t;
    }

    // ============================================================================
    // S-curve distance calculations (assuming zero entry/exit acceleration)
    // These are used for simple S-curve profiles where acceleration is 0 at junctions
    // ============================================================================

    // Distance for S-curve acceleration from v1 to v2
    // Profile: jerk+ → const accel → jerk-
    float accelDistance(float v1, float v2, float a_max, float jerk);

    // Distance for S-curve deceleration from v1 to v2
    // Profile: jerk- → const decel → jerk+
    float decelDistance(float v1, float v2, float a_max, float jerk);

    // ============================================================================
    // Extended S-curve calculations for continuous jerk at junctions
    // These account for non-zero entry/exit acceleration
    // ============================================================================

    // Distance with arbitrary entry/exit acceleration
    // Used for continuous jerk across block junctions
    float accelDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, float jerk);

    float decelDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, float jerk);

    // ============================================================================
    // Solver functions for planning passes
    // These find entry/exit conditions given distance constraints
    // ============================================================================

    // Find maximum entry speed that allows S-curve deceleration to exit conditions
    // Used in backward pass
    float solveMaxEntrySpeed(float distance, float v_exit, float a_exit, float a_max, float jerk);

    // Find maximum exit speed achievable via S-curve acceleration from entry conditions
    // Used in forward pass
    float solveMaxExitSpeed(float distance, float v_entry, float a_entry, float a_max, float jerk);

    // Find entry acceleration that produces smooth jerk transition
    // Given entry speed, exit conditions, and distance
    float solveEntryAccel(float distance, float v_entry, float v_exit, float a_exit, float a_max, float jerk);

    // ============================================================================
    // Profile planning
    // ============================================================================

    // Check if move is too short for full S-curve and compute achievable peak velocity
    float computePeakVelocity(float distance, float v_entry, float v_exit, float v_max, float a_max, float jerk);

    // Structure to hold computed 7-phase profile
    struct SCurveProfile {
        float   t[7];    // Time for each phase
        float   d[7];    // Distance for each phase
        float   v[8];    // Velocity at each phase boundary (v[0] = entry, v[7] = exit)
        float   a[8];    // Acceleration at each phase boundary
        float   v_peak;  // Peak velocity achieved
        float   total_distance;
        float   total_time;
        bool    degenerate;   // True if some phases were eliminated
        uint8_t phases_used;  // Bitmask of which phases are active
    };

    // Phase indices
    constexpr uint8_t PHASE_JERK_ACCEL_UP   = 0;
    constexpr uint8_t PHASE_CONST_ACCEL     = 1;
    constexpr uint8_t PHASE_JERK_ACCEL_DOWN = 2;
    constexpr uint8_t PHASE_CRUISE          = 3;
    constexpr uint8_t PHASE_JERK_DECEL_UP   = 4;
    constexpr uint8_t PHASE_CONST_DECEL     = 5;
    constexpr uint8_t PHASE_JERK_DECEL_DOWN = 6;

    // Plan a full 7-phase S-curve profile
    SCurveProfile planProfile(float distance, float v_entry, float a_entry, float v_exit, float a_exit, float v_max, float a_max, float jerk);

    // ============================================================================
    // Utility functions
    // ============================================================================

    // Clamp value to range
    inline float clamp(float value, float min_val, float max_val) {
        if (value < min_val)
            return min_val;
        if (value > max_val)
            return max_val;
        return value;
    }

    // Square a value
    inline float sq(float x) {
        return x * x;
    }

    // Safe square root (returns 0 for negative input)
    inline float safeSqrt(float x) {
        return (x > 0.0f) ? sqrtf(x) : 0.0f;
    }

}  // namespace SCurveMath
