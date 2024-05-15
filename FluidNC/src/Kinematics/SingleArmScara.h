// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	SingleArmScara.h

	This is a kinematic system to move a single arm type SCARA robot
*/

#include "Kinematics.h"
#include "../Limits.h" 

namespace Kinematics {
    class SingleArmScara : public KinematicSystem {
    public:
        SingleArmScara() = default;

        SingleArmScara(const SingleArmScara&)            = delete;
        SingleArmScara(SingleArmScara&&)                 = delete;
        SingleArmScara& operator=(const SingleArmScara&) = delete;
        SingleArmScara& operator=(SingleArmScara&&)      = delete;

        // Kinematic Interface

        void init() override;
        void init_position() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool transform_cartesian_to_motors(float* cartesian, float* motors) override;
        bool kinematics_homing(AxisMask& axisMask) override;
        
        virtual bool invalid_line(float* cartesian) override;
        virtual bool invalid_arc(float*            target,
                                 plan_line_data_t* pl_data,
                                 float*            position,
                                 float             center[3],
                                 float             radius,
                                 size_t            caxes[3],
                                 bool              is_clockwise_arc) override;

        // Configuration handlers:
        //void validate() const override {}
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "SingleArmScara"; }

        ~SingleArmScara() {}

    private:
        bool  xy_to_angles(float* cartesian, float* angles);
        float _last_motor_segment_end[MAX_N_AXIS];

        // config Parameters
        float _upper_arm_mm      = 65;
        float _forearm_mm        = 50;
        float _segment_length_mm = 1;
        bool  _elbow_motor       = true;  // is the motor at the elbow or belt driven
        float _orientation_rad   = 0;     // which way do the arms extend when fully extended. (bot is to the right)
    };
}  //  namespace Kinematics
