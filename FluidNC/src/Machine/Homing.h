// Copyright (c) 2021 - Stefan de Bruijn, Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Configuration/Configurable.h"
#include "System.h"    // AxisMask, MotorMask
#include "Protocol.h"  // ExecAlarm
#include <queue>

namespace Machine {
    class Homing : public Configuration::Configurable {
        static AxisMask _unhomed_axes;

    public:
        static enum Phase {
            None         = 0,
            PrePulloff   = 1,
            FastApproach = 2,
            Pulloff0     = 3,
            SlowApproach = 4,
            Pulloff1     = 5,
            Pulloff2     = 6,
            CycleDone    = 7,
        } _phase;
        static uint32_t _runs;

        static AxisMask unhomed_axes();
        static AxisMask direction_mask;

        static void set_axis_homed(axis_t axis);
        static void set_axis_unhomed(axis_t axis);
        static bool axis_is_homed(axis_t axis);
        static void set_all_axes_homed();
        static void set_all_axes_unhomed();

        Homing() = default;

        static const int AllCycles     = 0;   // Must be zero.
        static const int set_mpos_only = -1;  // If homing cycle is this value then don't move, just set mpos

        static bool approach() { return _phase == FastApproach || _phase == SlowApproach; }

        static void fail(ExecAlarm alarm);
        static void cycleStop();

        static void run_cycles(AxisMask axisMask);
        static void run_one_cycle(AxisMask axisMask);

        static AxisMask axis_mask_from_cycle(uint32_t cycle);
        static void     run(MotorMask remainingMotors, Phase phase);

        // The homing cycles are 1,2,3 etc.  0 means not homed as part of home-all,
        // but you can still home it manually with e.g. $HA
        int32_t  _cycle             = 0;     // what auto-homing cycle does this axis home on?
        bool     _allow_single_axis = true;  // Allow use of $H<axis> command on this axis
        bool     _positiveDirection = true;
        float    _mpos              = 0.0f;    // After homing this will be the mpos of the switch location
        float    _feedRate          = 50.0f;   // pulloff and second touch speed
        float    _seekRate          = 200.0f;  // this first approach speed
        uint32_t _settle_ms         = 250;     // ms settling time for homing switches after motion
        float    _seek_scaler       = 1.1f;    // multiplied by max travel for max homing distance on first touch
        float    _feed_scaler       = 1.1f;    // multiplier to pulloff for moving to switch after pulloff

        // Configuration system helpers:
        void validate() override { Assert(_cycle >= set_mpos_only, "Homing cycle must be defined"); }

        void group(Configuration::HandlerBase& handler) override {
            // @config cycle
            // @default 0
            // Which $H (home-all) pass this axis homes in. -1 (set_mpos_only): the axis
            // doesn't move at all -- mpos_mm is just assigned directly, for an axis with no
            // home switch. 0 (the default): excluded from $H, but still homeable
            // individually with $H<axis> (as long as allow_single_axis stays true). 1 or
            // higher: homes as part of $H; axes sharing the same cycle number home
            // simultaneously in that pass (not usable with CoreXY kinematics, since it
            // drives two motors per logical axis move). Typical convention: Z alone on
            // cycle 1, then X/Y together on cycle 2.
            handler.item("cycle", _cycle, set_mpos_only, MAX_N_AXIS);

            // @config allow_single_axis
            // @default true
            // Allows this axis to be homed individually with $H<axis> (e.g. $HX). Set to
            // false if there's no limit switch to home against, or to block the command --
            // a single-axis home unlocks the whole machine even though other axes may still
            // be unhomed, which soft limits can't protect against.
            handler.item("allow_single_axis", _allow_single_axis);

            // @config positive_direction
            // @default true
            // Direction this axis moves while homing: true moves toward higher position
            // values, false toward lower.
            handler.item("positive_direction", _positiveDirection);

            // @config mpos_mm
            // @default 0.0
            // Machine position assigned to this axis once homing (and pull-off) completes.
            // Set to 0 if the switch location should read as machine-position zero. No
            // range is enforced by this item() call -- it accepts any float.
            handler.item("mpos_mm", _mpos);

            // @config feed_mm_per_min
            // @default 50.0
            // Feed rate for the second (precise) touch of the limit switch, after the
            // initial fast approach and pull-off. Usually slow, since the axis is already
            // close to the switch -- a slower second touch tends to be more precise and
            // consistent.
            handler.item("feed_mm_per_min", _feedRate, 1.0, 100000.0);

            // @config seek_mm_per_min
            // @default 200.0
            // Feed rate for the initial fast approach that finds the limit switch's rough
            // position, before pulling off and re-touching at feed_mm_per_min for precision.
            handler.item("seek_mm_per_min", _seekRate, 1.0, 100000.0);

            // @config settle_ms
            // @default 250
            // Pause, in milliseconds, between homing cycles to let the machine settle
            // mechanically after the previous cycle's motion.
            handler.item("settle_ms", _settle_ms, 0, 1000);

            // @config seek_scaler
            // @default 1.1
            // Multiplied by max_travel_mm to get the maximum distance the initial seek move
            // will travel before failing if it hasn't yet reached the limit switch -- needs
            // to be a bit over 1.0 to allow for the extra distance a prior pull-off adds.
            handler.item("seek_scaler", _seek_scaler, 1.0, 100.0);

            // @config feed_scaler
            // @default 1.1
            // Multiplied by the motor's own pulloff_mm to get the max distance the axis
            // will travel back toward the switch, after the first pull-off, before the
            // precise second touch fails. A switch with a lot of positional variability (or
            // sensorless homing) may need a larger value to reliably trigger the second
            // touch.
            handler.item("feed_scaler", _feed_scaler, 1.0, 100.0);
        }

        void init() {}

        static void set_mpos();

        static bool needsPulloff2(MotorMask motors);

        static void limitReached();

    private:
        static uint32_t planMove(AxisMask axisMask, MotorMask motors, Phase phase, float* target, float& rate);

        static void done();
        static void runPhase();
        static void nextPhase();
        static void nextCycle();

        static MotorMask _cycleMotors;  // Motors for this cycle
        static MotorMask _phaseMotors;  // Motors still running in this phase
        static AxisMask  _cycleAxes;    // Axes for this cycle
        static AxisMask  _phaseAxes;    // Axes still active in this phase

        static std::queue<int> _remainingCycles;

        static uint32_t _settling_ms;

        static const char* _phaseNames[];
        static const char* phaseName(Phase phase) { return _phaseNames[static_cast<int>(phase)]; }
    };
}
