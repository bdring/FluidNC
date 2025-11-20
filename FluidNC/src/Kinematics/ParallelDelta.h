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
        ParallelDelta(const char* name) : Cartesian(name) {}

        ParallelDelta(const ParallelDelta&)            = delete;
        ParallelDelta(ParallelDelta&&)                 = delete;
        ParallelDelta& operator=(const ParallelDelta&) = delete;
        ParallelDelta& operator=(ParallelDelta&&)      = delete;

        // Kinematic Interface
        virtual void init() override;
        virtual void init_position() override;
        //bool canHome(AxisMask& axisMask) override;
        bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void motors_to_cartesian(float* cartesian, float* motors, axis_t n_axis) override;
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
                                 axis_t            caxes[3],
                                 bool              is_clockwise_arc,
                                 uint32_t          rotations) override;

        bool canHome(AxisMask axisMask) override;

        // limitReached() is inherited from Cartesian
        // releaseMotors() is inherited from Cartesian

        // Configuration handlers:
        //void         validate() const override {}
        virtual void group(Configuration::HandlerBase& handler) override;
        void         afterParse() override {}

        ~ParallelDelta() {}

    private:
        //  Config items Using geometry names from the published kinematics rather than typical Fluid Style
        // To make the math easier to compare with the code
        float rf = 70.0;     // crank_mm - The length of the crank arm on the motor
        float f  = 179.437;  // base_triangle_mm
        float re = 133.50;   // linkage_mm
        float e  = 86.603;   // end_effector_triangle_mm

        float _kinematic_segment_len_mm = 1.0;  // the maximum segment length the move is broken into
        bool  _use_servos               = false;  // servo use a special homing

        float _homing_degrees = 0.0;
        float _up_degrees     = -30.0;
        float _down_degrees   = 90.0;

        float _last_motor_pos[MAX_N_AXIS] = { 0 };
        float _mpos_offset[3]             = { 0 };

        bool delta_calcAngleYZ(float x0, float y0, float z0, float& theta);

        void motorVector(AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate, uint32_t& settle_ms);
        void homing_move(AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, uint32_t settling_ms) override;
        void set_homed_mpos(float* mpos);
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        inline float pos_to_radians(float pos) { return pos * (M_PI / 180.0); }

        inline float radians_to_pos(float radians) { return radians * (180.0 / M_PI); }

        inline float degrees_to_pos(float degrees) { return radians_to_pos(degrees * M_PI / 180.0); }

    protected:
    };
}  //  namespace Kinematics
