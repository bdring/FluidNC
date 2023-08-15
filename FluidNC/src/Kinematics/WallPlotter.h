// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	WallPlotter.h

	This is a kinematic system to move a puck suspended by two cords by adjusting the cord length.
*/

#include "Kinematics.h"

namespace Kinematics {
    class WallPlotter : public KinematicSystem {
    public:
        WallPlotter() = default;

        WallPlotter(const WallPlotter&) = delete;
        WallPlotter(WallPlotter&&)      = delete;
        WallPlotter& operator=(const WallPlotter&) = delete;
        WallPlotter& operator=(WallPlotter&&) = delete;

        // Kinematic Interface

        void init() override;
        bool canHome(AxisMask axisMask) override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool transform_cartesian_to_motors(float* cartesian, float* motors) override;
        bool kinematics_homing(AxisMask& axisMask) override;

        // Configuration handlers:
        void validate() override {}
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "WallPlotter"; }

        ~WallPlotter() {}

    private:
        void lengths_to_xy(float left_length, float right_length, float& x, float& y);
        void xy_to_lengths(float x, float y, float& left_length, float& right_length);

        // State
        float zero_left;   //  The left cord offset corresponding to cartesian (0, 0).
        float zero_right;  //  The right cord offset corresponding to cartesian (0, 0).
        float last_motor_segment_end[MAX_N_AXIS];

        // Parameters
        int   _left_axis     = 0;
        float _left_anchor_x = -100;
        float _left_anchor_y = 100;

        int   _right_axis     = 1;
        float _right_anchor_x = 100;
        float _right_anchor_y = 100;
        float _segment_length = 10;
    };
}  //  namespace Kinematics
