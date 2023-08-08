// Copyright (c) 2021 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	CoreXY.h

	This implements CoreXY Kinematics

    Ref: https://corexy.com/

*/

#include "Kinematics.h"
#include "Cartesian.h"

namespace Kinematics {
    class CoreXY : public Cartesian {
    public:
        CoreXY() = default;

        CoreXY(const CoreXY&) = delete;
        CoreXY(CoreXY&&)      = delete;
        CoreXY& operator=(const CoreXY&) = delete;
        CoreXY& operator=(CoreXY&&) = delete;

        // Kinematic Interface

        virtual void init() override;
        bool         cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void         motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited);

        // Configuration handlers:
        void         validate() override {}
        virtual void group(Configuration::HandlerBase& handler) override;
        void         afterParse() override {}

        bool transform_cartesian_to_motors(float* motors, float* cartesian) override;

        // Name of the configurable. Must match the name registered in the cpp file.
        virtual const char* name() const override { return "CoreXY"; }

        ~CoreXY() {}

    private:
        void lengths_to_xy(float left_length, float right_length, float& x, float& y);
        void xy_to_lengths(float x, float y, float& left_length, float& right_length);

        void plan_homing_move(AxisMask axisMask, bool approach, bool seek);

    protected:
        float _x_scaler = 1.0;
    };
}  //  namespace Kinematics
