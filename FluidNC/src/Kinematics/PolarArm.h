// Copyright (c) 2022 -	Bob Klosinski
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	PolarArm.h
	This is a kinematic system to move a puck suspended by two cords by adjusting the cord length.
*/

#include "Kinematics.h"
#include "Cartesian.h"

namespace Kinematics {
    class PolarArm : public KinematicSystem {
    public:
        PolarArm(const char* name) : KinematicSystem(name) {}

        PolarArm(const PolarArm&)            = delete;
        PolarArm(PolarArm&&)                 = delete;
        PolarArm& operator=(const PolarArm&) = delete;
        PolarArm& operator=(PolarArm&&)      = delete;

        // Kinematic Interface

        void init() override;
        bool canHome(AxisMask axisMask) override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool transform_cartesian_to_motors(float* cartesian, float* motors) override;
        bool kinematics_homing(AxisMask& cycle_mask) override;

        // Configuration handlers:
        void validate() override {}
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override {}

        ~PolarArm() {}

    private:
        float three_axis_dist(float* point1, float* point2);
        float calculate_x_offset(float y_position);
        // State
        float arm_length_squared;  //  The left cord offset corresponding to cartesian (0, 0).

        // Parameters
        float _arm_length          = 803.275;
        float _straight_y_position = 228.6;
        float _segment_length      = 1.0;
    };
}  //  namespace Kinematics
