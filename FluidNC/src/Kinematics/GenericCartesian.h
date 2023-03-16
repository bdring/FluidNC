// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2023 -	Vlad A.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	GenericCartesian.h

	This is a kinematic system for where the motors operate in the cartesian space.

    An abstract class which should be a base for diferent transformation cases.

    Unlike Cartisian kinamatic system, this system allow motor space to vary from machine space. 
	Transformation from system space to motor space is defined as matrix.
    All axis must by linearly independent. Matrix has to be inversable.

*/

#include "Kinematics.h"

#define LOG_MATRIX_CONTENT

namespace Kinematics {

    class GenericCartesian : public KinematicSystem {
    public:
        GenericCartesian() = default;

        GenericCartesian(const GenericCartesian&)            = delete;
        GenericCartesian(GenericCartesian&&)                 = delete;
        GenericCartesian& operator=(const GenericCartesian&) = delete;
        GenericCartesian& operator=(GenericCartesian&&)      = delete;

        // Kinematic Interface
        virtual void init() override;
        virtual void init_position() override;

        virtual bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) override;
        void         motors_to_cartesian(float* cartesian, float* motors, int n_axis) override;
        void         transform_cartesian_to_motors(float* cartesian, float* motors) override;

        bool canHome(AxisMask axisMask) override;
        void releaseMotors(AxisMask axisMask, MotorMask motors) override;
        bool limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) override;

        // Configuration handlers:
//        void afterParse() override {}
//        void group(Configuration::HandlerBase& handler) override;
//        void validate() override {}

        // Name of the configurable. Must match the name registered in the cpp file.
//        const char* name() const override { return "Generic Cartesian"; }

    protected:
        template <typename number>
        class Mtx {
            uint    _pitch;
            uint    _lines;
            number* _buffer;

        public:
            Mtx(const uint row, const uint col) : _pitch(col), _lines(row) { _buffer = new number[_pitch * _lines]; };

            void allocate() {  }
            void deallocate() {
            }

            number* getBuffer() { return _buffer; }
            number  value(const uint row, const uint col) const { return _buffer[row * _pitch + col]; }
            number* ptr(const uint row, const uint col) { return _buffer + row * _pitch + col; }
            void    transform(number* to, const number* from) const;

            void dumpRow(const uint idx) const;
            void dump() const;

            ~Mtx() {
                if ( _buffer )
                    delete[] _buffer;
            }
        };

        float _buffer[6];
        Mtx<float>* _mtx = nullptr;
        Mtx<float>* _rev = nullptr;

        bool GJ_invertMatrix( const uint size, const Mtx<float>* const A, Mtx<float>* const B );

        ~GenericCartesian();
    };
}  //  namespace Kinematics
