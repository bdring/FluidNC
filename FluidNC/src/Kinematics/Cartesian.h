// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	Cartesian.h

	This is a kinematic system for where the motors operate in the cartesian space.
*/

#include "Kinematics.h"

namespace Kinematics {
    class Cartesian : public KinematicSystem {
    public:
        Cartesian() = default;

        Cartesian(const Cartesian&) = delete;
        Cartesian(Cartesian&&)      = delete;
        Cartesian& operator=(const Cartesian&) = delete;
        Cartesian& operator=(Cartesian&&) = delete;

        // Kinematic Interface

        void config_message() override;
        void init() override;
        bool kinematics_homing(AxisMask cycle_mask) override;
        void kinematics_post_homing() override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool limitsCheckTravel(float* target) override;

        // Configuration handlers:
        void validate() const override {}
        void group(Configuration::HandlerBase& handler) override {}
        void afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Cartesian"; }

        ~Cartesian() {}
    };
} //  namespace Kinematics
