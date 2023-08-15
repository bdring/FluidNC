// Copyright (c) 2021 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*

	This implements Parallel Delta Kinematics

*/

#include "Kinematics.h"
#include "Cartesian.h"

// M_PI is not defined in standard C/C++ but some compilers
// support it anyway.  The following suppresses Intellisense
// problem reports.
#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

namespace Kinematics {

    class ParallelDelta : public Cartesian {
    public:
        ParallelDelta() = default;

        ParallelDelta(const ParallelDelta&)            = delete;
        ParallelDelta(ParallelDelta&&)                 = delete;
        ParallelDelta& operator=(const ParallelDelta&) = delete;
        ParallelDelta& operator=(ParallelDelta&&)      = delete;

        // Kinematic Interface
        virtual void init() override;
        virtual void init_position() override;
        //bool canHome(AxisMask& axisMask) override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        bool transform_cartesian_to_motors(float* motors, float* cartesian) override;
        //bool soft_limit_error_exists(float* cartesian) override;
        bool         kinematics_homing(AxisMask& axisMask) override;
        virtual void constrain_jog(float* cartesian, plan_line_data_t* pl_data, float* position) override;
        virtual bool invalid_line(float* cartesian) override;
        virtual bool invalid_arc(float*            target,
                                 plan_line_data_t* pl_data,
                                 float*            position,
                                 float             center[3],
                                 float             radius,
                                 size_t            caxes[3],
                                 bool              is_clockwise_arc) override;

        void releaseMotors(AxisMask axisMask, MotorMask motors) override;

        // Configuration handlers:
        //void         validate() const override {}
        virtual void group(Configuration::HandlerBase& handler) override;
        void         afterParse() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
        virtual const char* name() const override { return "parallel_delta"; }

        ~ParallelDelta() {}

    private:
        //  Config items Using geometry names from the published kinematics rather than typical Fluid Style
        // To make the math easier to compare with the code
        float rf = 70.0;  // The length of the crank arm on the motor
        float f  = 179.437;
        float re = 133.50;
        float e  = 86.603;

        float _kinematic_segment_len_mm = 1.0;  // the maximun segment length the move is broken into
        bool  _softLimits               = false;
        float _homing_mpos              = 0.0;
        float _max_z                    = 0.0;
        bool  _use_servos               = true;  // servo use a special homing

        bool  delta_calcAngleYZ(float x0, float y0, float z0, float& theta);
        float three_axis_dist(float* point1, float* point2);

    protected:
    };
}  //  namespace Kinematics
