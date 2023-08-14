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

        virtual void constrain_jog(float* cartesian, plan_line_data_t* pl_data, float* position) override;
        virtual bool invalid_line(float* cartesian) override;
        virtual bool invalid_arc(float*            target,
                                 plan_line_data_t* pl_data,
                                 float*            position,
                                 float             center[3],
                                 float             radius,
                                 size_t            caxes[3],
                                 bool              is_clockwise_arc) override;

        virtual bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        virtual void init() override;
        virtual void init_position() override;
        void         motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool         transform_cartesian_to_motors(float* cartesian, float* motors) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;
        virtual bool kinematics_homing(AxisMask& axisMask) override;

        // Configuration handlers:
        void afterParse() override {}
        void group(Configuration::HandlerBase& handler) override {}
        void validate() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Cartesian"; }

    protected:
        ~Cartesian() {}
    };
}  //  namespace Kinematics
