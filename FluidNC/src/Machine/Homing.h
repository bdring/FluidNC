// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Configuration/Configurable.h"
#include "src/System.h"    // AxisMask, MotorMask
#include "src/Protocol.h"  // ExecAlarm
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

        static AxisMask unhomed_axes();

        static void set_axis_homed(size_t axis);
        static void set_axis_unhomed(size_t axis);
        static bool axis_is_homed(size_t axis);
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

        static AxisMask axis_mask_from_cycle(int cycle);
        static void     run(MotorMask remainingMotors, Phase phase);

        static void startMove(AxisMask axisMask, MotorMask motors, Phase phase, uint32_t& settle_ms);
        static void axisVector(AxisMask axisMask, MotorMask motors, Phase phase, float* target, float& rate, uint32_t& settle_ms);

        // The homing cycles are 1,2,3 etc.  0 means not homed as part of home-all,
        // but you can still home it manually with e.g. $HA
        int      _cycle             = 0;     // what auto-homing cycle does this axis home on?
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
            handler.item("cycle", _cycle, set_mpos_only, MAX_N_AXIS);
            handler.item("allow_single_axis", _allow_single_axis);
            handler.item("positive_direction", _positiveDirection);
            handler.item("mpos_mm", _mpos);
            handler.item("feed_mm_per_min", _feedRate, 1.0, 100000.0);
            handler.item("seek_mm_per_min", _seekRate, 1.0, 100000.0);
            handler.item("settle_ms", _settle_ms, 0, 1000);
            handler.item("seek_scaler", _seek_scaler, 1.0, 100.0);
            handler.item("feed_scaler", _feed_scaler, 1.0, 100.0);
        }

        void init() {}

        static void set_mpos();

        static const int REPORT_LINE_NUMBER = 0;

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
