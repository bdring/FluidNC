// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2023 -	Vlad A.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	GenericCartesian.h

	This is a kinematic system for where the motors operate in the cartesian space.

    An abstract class which should be a base for diferent transformation cases.

    Unlike Cartisian kinematic system which uses the identity transform between the
    axis and motor spaces, this system uses an arbitrary linear transform 
    defined by an invertible matrix.

*/

#include "Kinematics.h"

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

    protected:
        template <typename number>
        class Mtx {
            size_t  _pitch; // Here, it's equal the number of columns in a matrix row
            size_t  _lines; // The number of rows
            number* _buffer;

        public:
            Mtx(const size_t row, const size_t col) : _pitch(col), _lines(row) { _buffer = new number[_pitch * _lines]; };

            void allocate() {}
            void deallocate() {}

            number* getBuffer() { return _buffer; }
            number  value(const size_t row, const size_t col) const { return _buffer[row * _pitch + col]; }
            number* ptr(const size_t row, const size_t col) { return _buffer + row * _pitch + col; }
            void    transform(number* to, const number* from) const;

            void dumpRow(const size_t idx) const;
            void dump() const;

            ~Mtx() {
                if (_buffer) {
                    delete[] _buffer;
                }
            }
        };

        float       _buffer[6];
        Mtx<float>* _mtx = nullptr;
        Mtx<float>* _rev = nullptr;

        bool GJ_invertMatrix(const size_t size, const Mtx<float>* const A, Mtx<float>* const B);

        ~GenericCartesian();
    };
}  //  namespace Kinematics
