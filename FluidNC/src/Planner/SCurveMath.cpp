// Copyright (c) 2024-2026 - Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SCurveMath.h"

#include <algorithm>

namespace SCurveMath {

    // ============================================================================
    // S-curve distance calculations (zero entry/exit acceleration)
    // ============================================================================
    float accelDistance(float v1, float v2, float a_max, float jerk) {
        if (v2 <= v1)
            return 0.0f;  // No acceleration needed

        // Time for each jerk phase (to ramp accel from 0 to a_max or back)
        float t_jerk = a_max / jerk;

        // Velocity change during one jerk phase: Δv = ½*j*t²
        float v_jerk = 0.5f * jerk * t_jerk * t_jerk;

        float delta_v = v2 - v1;

        // Check if we have a triangular profile (not enough delta_v for full jerk phases)
        if (delta_v <= 2.0f * v_jerk) {
            // Triangular S-curve: jerk+ then jerk- with no constant accel phase
            // delta_v = j * t² (symmetric triangle)
            float t = safeSqrt(delta_v / jerk);

            // Distance = 2 * (v1*t + ⅙*j*t³)
            float d1    = jerkDistance(v1, 0.0f, jerk, t);
            float v_mid = v1 + 0.5f * jerk * t * t;
            float d2    = jerkDistance(v_mid, jerk * t, -jerk, t);

            return d1 + d2;
        }

        // Full 3-phase S-curve: jerk+ → const accel → jerk-
        // Phase 1: jerk+ from a=0 to a=a_max
        float d1            = jerkDistance(v1, 0.0f, jerk, t_jerk);
        float v_after_jerk1 = v1 + v_jerk;

        // Phase 3: jerk- from a=a_max to a=0
        float v_before_jerk2 = v2 - v_jerk;
        float d3             = jerkDistance(v_before_jerk2, a_max, -jerk, t_jerk);

        // Phase 2: constant acceleration at a_max
        // v² = v₀² + 2*a*d → d = (v² - v₀²) / (2*a)
        float d2 = (sq(v_before_jerk2) - sq(v_after_jerk1)) / (2.0f * a_max);

        return d1 + d2 + d3;
    }

    float decelDistance(float v1, float v2, float a_max, float jerk) {
        if (v1 <= v2)
            return 0.0f;  // No deceleration needed

        float t_jerk = a_max / jerk;
        float v_jerk = 0.5f * jerk * t_jerk * t_jerk;

        float delta_v = v1 - v2;

        // Triangular decel profile
        if (delta_v <= 2.0f * v_jerk) {
            float t = safeSqrt(delta_v / jerk);

            // Distance for symmetric jerk- then jerk+ triangle
            float d1    = jerkDistance(v1, 0.0f, -jerk, t);
            float v_mid = v1 - 0.5f * jerk * t * t;
            float d2    = jerkDistance(v_mid, -jerk * t, jerk, t);

            return d1 + d2;
        }

        // Full 3-phase: jerk- → const decel → jerk+
        // Phase 1: jerk- from a=0 to a=-a_max
        float d1            = jerkDistance(v1, 0.0f, -jerk, t_jerk);
        float v_after_jerk1 = v1 - v_jerk;

        // Phase 3: jerk+ from a=-a_max to a=0
        float v_before_jerk2 = v2 + v_jerk;
        float d3             = jerkDistance(v_before_jerk2, -a_max, jerk, t_jerk);

        // Phase 2: constant deceleration at -a_max
        float d2 = (sq(v_after_jerk1) - sq(v_before_jerk2)) / (2.0f * a_max);

        return d1 + d2 + d3;
    }

    // ============================================================================
    // Extended S-curve calculations for continuous jerk
    // ============================================================================
    float accelDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, float jerk) {
        // Handle the case where we start with non-zero acceleration
        // and need to end with a specific acceleration

        if (v_exit <= v_entry && a_entry >= 0.0f && a_exit >= 0.0f) {
            return 0.0f;  // Can't accelerate to lower speed
        }

        // Time to ramp from a_entry to a_max
        float t1 = (a_max - a_entry) / jerk;
        if (t1 < 0.0f)
            t1 = 0.0f;

        // Time to ramp from a_max to a_exit
        float t3 = (a_max - a_exit) / jerk;
        if (t3 < 0.0f)
            t3 = 0.0f;

        // Velocity and distance during phase 1 (jerk+)
        float v1_end = jerkVelocity(v_entry, a_entry, jerk, t1);
        float d1     = jerkDistance(v_entry, a_entry, jerk, t1);

        // Velocity at start of phase 3 (before jerk-)
        // During phase 3: v(t) = v3_start + a_max*t - 0.5*jerk*t^2
        // At t=t3: v_exit = v3_start + a_max*t3 - 0.5*jerk*t3^2
        // Therefore: v3_start = v_exit - a_max*t3 + 0.5*jerk*t3^2
        float v3_start = v_exit - a_max * t3 + 0.5f * jerk * t3 * t3;

        // Distance during phase 3 (jerk-)
        float d3 = jerkDistance(v3_start, a_max, -jerk, t3);

        // Check if we need constant accel phase
        if (v1_end >= v3_start) {
            // Triangular - no constant accel phase needed
            // For symmetric triangle: delta_v = jerk * t^2, so t = sqrt(delta_v / jerk)
            float delta_v = v_exit - v_entry;
            if (delta_v <= 0.0f)
                return 0.0f;

            // Same triangular calculation as accelDistance
            float t     = safeSqrt(delta_v / jerk);
            float d1    = jerkDistance(v_entry, a_entry, jerk, t);
            float v_mid = jerkVelocity(v_entry, a_entry, jerk, t);
            float a_mid = jerkAccel(a_entry, jerk, t);
            float d2    = jerkDistance(v_mid, a_mid, -jerk, t);
            return d1 + d2;
        }

        // Phase 2: constant acceleration at a_max
        float d2 = (sq(v3_start) - sq(v1_end)) / (2.0f * a_max);

        return d1 + d2 + d3;
    }

    float decelDistanceWithAccel(float v_entry, float a_entry, float v_exit, float a_exit, float a_max, float jerk) {
        if (v_entry <= v_exit && a_entry <= 0.0f && a_exit <= 0.0f) {
            return 0.0f;  // Can't decelerate to higher speed
        }

        // Time to ramp from a_entry to -a_max (negative acceleration for decel)
        float t1 = (a_entry + a_max) / jerk;
        if (t1 < 0.0f)
            t1 = 0.0f;

        // Time to ramp from -a_max to a_exit
        float t3 = (a_max + a_exit) / jerk;
        if (t3 < 0.0f)
            t3 = 0.0f;

        // Phase 1: jerk- (decreasing acceleration toward -a_max)
        float v1_end = jerkVelocity(v_entry, a_entry, -jerk, t1);
        float d1     = jerkDistance(v_entry, a_entry, -jerk, t1);

        // Phase 3: jerk+ (increasing acceleration from -a_max toward a_exit)
        // During phase 3: v(t) = v3_start - a_max*t + 0.5*jerk*t^2
        // At t=t3: v_exit = v3_start - a_max*t3 + 0.5*jerk*t3^2
        // Therefore: v3_start = v_exit + a_max*t3 - 0.5*jerk*t3^2
        float v3_start = v_exit + a_max * t3 - 0.5f * jerk * t3 * t3;
        float d3       = jerkDistance(v3_start, -a_max, jerk, t3);

        // Check if triangular
        if (v1_end <= v3_start) {
            // Triangular profile
            // For symmetric triangle: delta_v = jerk * t^2, so t = sqrt(delta_v / jerk)
            float delta_v = v_entry - v_exit;
            if (delta_v <= 0.0f)
                return 0.0f;

            // Same triangular calculation as decelDistance
            float t     = safeSqrt(delta_v / jerk);
            float d1    = jerkDistance(v_entry, a_entry, -jerk, t);
            float v_mid = jerkVelocity(v_entry, a_entry, -jerk, t);
            float a_mid = jerkAccel(a_entry, -jerk, t);
            float d2    = jerkDistance(v_mid, a_mid, jerk, t);
            return d1 + d2;
        }

        // Phase 2: constant deceleration at -a_max
        float d2 = (sq(v1_end) - sq(v3_start)) / (2.0f * a_max);

        return d1 + d2 + d3;
    }

    // ============================================================================
    // Solver functions
    // ============================================================================

    float solveMaxEntrySpeed(float distance, float v_exit, float a_exit, float a_max, float jerk) {
        // Binary search for max entry speed that can decelerate to v_exit in distance
        // This is conservative - we want the highest speed that definitely works

        float v_low  = v_exit;
        float v_high = v_exit + safeSqrt(2.0f * a_max * distance) * 1.5f;  // Upper bound estimate

        const int   MAX_ITER  = 20;
        const float TOLERANCE = 0.1f;  // mm/min tolerance

        for (int i = 0; i < MAX_ITER; i++) {
            float v_mid    = (v_low + v_high) / 2.0f;
            float d_needed = decelDistanceWithAccel(v_mid, 0.0f, v_exit, a_exit, a_max, jerk);

            if (d_needed > distance) {
                v_high = v_mid;  // Too fast, need to slow down
            } else {
                v_low = v_mid;  // Can go faster
            }

            if (v_high - v_low < TOLERANCE)
                break;
        }

        return v_low;  // Return conservative (lower) value
    }

    float solveMaxExitSpeed(float distance, float v_entry, float a_entry, float a_max, float jerk) {
        // Binary search for max exit speed achievable from entry conditions

        float v_low  = v_entry;
        float v_high = v_entry + safeSqrt(2.0f * a_max * distance) * 1.5f;

        const int   MAX_ITER  = 20;
        const float TOLERANCE = 0.1f;

        for (int i = 0; i < MAX_ITER; i++) {
            float v_mid    = (v_low + v_high) / 2.0f;
            float d_needed = accelDistanceWithAccel(v_entry, a_entry, v_mid, 0.0f, a_max, jerk);

            if (d_needed > distance) {
                v_high = v_mid;
            } else {
                v_low = v_mid;
            }

            if (v_high - v_low < TOLERANCE)
                break;
        }

        return v_low;
    }

    float solveEntryAccel(float distance, float v_entry, float v_exit, float a_exit, float a_max, float jerk) {
        // Find entry acceleration that produces the smoothest transition
        // For now, return 0 (simplified - assumes we want zero accel at junctions)
        // A more sophisticated implementation would solve for continuous jerk

        // The entry acceleration should be such that we can smoothly transition
        // to the required exit conditions. For continuous jerk, this typically
        // means the entry accel of the next block equals the exit accel of this block.

        // Simplified: return 0 for now
        return 0.0f;
    }

    float computePeakVelocity(float distance, float v_entry, float v_exit, float v_max, float a_max, float jerk) {
        // Check if we can reach v_max given the distance constraints

        float d_accel = accelDistance(v_entry, v_max, a_max, jerk);
        float d_decel = decelDistance(v_max, v_exit, a_max, jerk);

        if (d_accel + d_decel <= distance) {
            return v_max;  // Can reach full speed
        }

        // Binary search for achievable peak velocity
        float v_low  = std::max(v_entry, v_exit);
        float v_high = v_max;

        const int   MAX_ITER  = 20;
        const float TOLERANCE = 0.1f;

        for (int i = 0; i < MAX_ITER; i++) {
            float v_mid   = (v_low + v_high) / 2.0f;
            float d_total = accelDistance(v_entry, v_mid, a_max, jerk) + decelDistance(v_mid, v_exit, a_max, jerk);

            if (d_total > distance) {
                v_high = v_mid;
            } else {
                v_low = v_mid;
            }

            if (v_high - v_low < TOLERANCE)
                break;
        }

        return v_low;
    }

    // ============================================================================
    // Profile planning
    // ============================================================================
    SCurveProfile planProfile(float distance, float v_entry, float a_entry, float v_exit, float a_exit, float v_max, float a_max, float jerk) {
        SCurveProfile profile = {};

        profile.v[0] = v_entry;
        profile.a[0] = a_entry;

        // Compute achievable peak velocity
        profile.v_peak = computePeakVelocity(distance, v_entry, v_exit, v_max, a_max, jerk);

        // Maximum time for jerk phases (time to reach a_max from 0)
        float t_jerk_max = a_max / jerk;

        // Velocity change possible in one full jerk phase
        float v_jerk_max = 0.5f * jerk * t_jerk_max * t_jerk_max;

        // Check if this is a triangular acceleration profile (can't reach a_max)
        float delta_v_accel    = profile.v_peak - v_entry;
        bool  triangular_accel = (delta_v_accel <= 2.0f * v_jerk_max) && (delta_v_accel > 0.0f);

        // Check if this is a triangular deceleration profile
        float delta_v_decel    = profile.v_peak - v_exit;
        bool  triangular_decel = (delta_v_decel <= 2.0f * v_jerk_max) && (delta_v_decel > 0.0f);

        // === Acceleration phases (0, 1, 2) ===

        if (delta_v_accel <= 0.0f) {
            // No acceleration needed
            profile.t[0] = profile.t[1] = profile.t[2] = 0.0f;
            profile.d[0] = profile.d[1] = profile.d[2] = 0.0f;
            profile.v[1] = profile.v[2] = profile.v[3] = v_entry;
            profile.a[1] = profile.a[2] = profile.a[3] = a_entry;
        } else if (triangular_accel) {
            // Triangular acceleration: phase 0 (jerk+) and phase 2 (jerk-), no phase 1
            float t_accel = safeSqrt(delta_v_accel / jerk);

            profile.t[0] = t_accel;
            profile.t[1] = 0.0f;
            profile.t[2] = t_accel;

            profile.v[1] = jerkVelocity(v_entry, a_entry, jerk, t_accel);
            profile.a[1] = jerkAccel(a_entry, jerk, t_accel);
            profile.d[0] = jerkDistance(v_entry, a_entry, jerk, t_accel);

            profile.v[2] = profile.v[1];
            profile.a[2] = profile.a[1];
            profile.d[1] = 0.0f;

            profile.v[3] = jerkVelocity(profile.v[2], profile.a[2], -jerk, t_accel);
            profile.a[3] = jerkAccel(profile.a[2], -jerk, t_accel);
            profile.d[2] = jerkDistance(profile.v[2], profile.a[2], -jerk, t_accel);

            profile.degenerate = true;
        } else {
            // Full trapezoidal acceleration: phase 0, phase 1 (const), phase 2
            float t0 = (a_max - a_entry) / jerk;
            if (t0 < 0.0f)
                t0 = 0.0f;

            profile.t[0] = t0;
            profile.v[1] = jerkVelocity(v_entry, a_entry, jerk, t0);
            profile.a[1] = a_max;
            profile.d[0] = jerkDistance(v_entry, a_entry, jerk, t0);

            float t2       = t_jerk_max;
            float v2_start = profile.v_peak - 0.5f * jerk * t2 * t2;  // Working backward

            if (v2_start > profile.v[1]) {
                profile.t[1] = (v2_start - profile.v[1]) / a_max;
                profile.d[1] = profile.t[1] * (profile.v[1] + v2_start) / 2.0f;
                profile.v[2] = v2_start;
            } else {
                profile.t[1] = 0.0f;
                profile.d[1] = 0.0f;
                profile.v[2] = profile.v[1];
            }
            profile.a[2] = a_max;

            profile.t[2] = t2;
            profile.v[3] = profile.v_peak;
            profile.a[3] = 0.0f;
            profile.d[2] = jerkDistance(profile.v[2], a_max, -jerk, t2);
        }

        // === Cruise phase (3) ===

        float d_accel  = profile.d[0] + profile.d[1] + profile.d[2];
        float d_decel  = decelDistance(profile.v_peak, v_exit, a_max, jerk);
        float d_cruise = distance - d_accel - d_decel;

        if (d_cruise > 0.0f && profile.v_peak > 0.0f) {
            profile.t[3] = d_cruise / profile.v_peak;
            profile.d[3] = d_cruise;
            profile.v[4] = profile.v_peak;
            profile.a[4] = 0.0f;
        } else {
            profile.t[3]       = 0.0f;
            profile.d[3]       = 0.0f;
            profile.v[4]       = profile.v_peak;
            profile.a[4]       = 0.0f;
            profile.degenerate = true;
        }

        // === Deceleration phases (4, 5, 6) ===

        if (delta_v_decel <= 0.0f) {
            // No deceleration needed
            profile.t[4] = profile.t[5] = profile.t[6] = 0.0f;
            profile.d[4] = profile.d[5] = profile.d[6] = 0.0f;
            profile.v[5] = profile.v[6] = profile.v[7] = profile.v[4];
            profile.a[5] = profile.a[6] = profile.a[7] = 0.0f;
        } else if (triangular_decel) {
            // Triangular deceleration: phase 4 (jerk-) and phase 6 (jerk+), no phase 5
            float t_decel = safeSqrt(delta_v_decel / jerk);

            profile.t[4] = t_decel;
            profile.t[5] = 0.0f;
            profile.t[6] = t_decel;

            profile.v[5] = jerkVelocity(profile.v[4], 0.0f, -jerk, t_decel);
            profile.a[5] = jerkAccel(0.0f, -jerk, t_decel);
            profile.d[4] = jerkDistance(profile.v[4], 0.0f, -jerk, t_decel);

            profile.v[6] = profile.v[5];
            profile.a[6] = profile.a[5];
            profile.d[5] = 0.0f;

            profile.v[7] = jerkVelocity(profile.v[6], profile.a[6], jerk, t_decel);
            profile.a[7] = jerkAccel(profile.a[6], jerk, t_decel);
            profile.d[6] = jerkDistance(profile.v[6], profile.a[6], jerk, t_decel);
        } else {
            // Full trapezoidal deceleration: phase 4, phase 5 (const), phase 6
            float t4     = t_jerk_max;
            profile.t[4] = t4;
            profile.v[5] = jerkVelocity(profile.v[4], 0.0f, -jerk, t4);
            profile.a[5] = -a_max;
            profile.d[4] = jerkDistance(profile.v[4], 0.0f, -jerk, t4);

            float t6 = (a_max + a_exit) / jerk;
            if (t6 < 0.0f)
                t6 = 0.0f;
            profile.t[6] = t6;

            // v[6] = v_exit + a_max*t6 - 0.5*jerk*t6^2 (working backward)
            float v5_end = v_exit + a_max * t6 - 0.5f * jerk * t6 * t6;
            if (v5_end < profile.v[5]) {
                profile.t[5] = (profile.v[5] - v5_end) / a_max;
                profile.d[5] = profile.t[5] * (profile.v[5] + v5_end) / 2.0f;
                profile.v[6] = v5_end;
            } else {
                profile.t[5] = 0.0f;
                profile.d[5] = 0.0f;
                profile.v[6] = profile.v[5];
            }
            profile.a[6] = -a_max;

            profile.v[7] = v_exit;
            profile.a[7] = a_exit;
            profile.d[6] = jerkDistance(profile.v[6], -a_max, jerk, t6);
        }

        // Calculate totals
        profile.total_distance = 0.0f;
        profile.total_time     = 0.0f;
        profile.phases_used    = 0;

        for (int i = 0; i < 7; i++) {
            profile.total_distance += profile.d[i];
            profile.total_time += profile.t[i];
            if (profile.t[i] > 0.0001f) {
                profile.phases_used |= (1 << i);
            }
        }

        return profile;
    }

}  // namespace SCurveMath
