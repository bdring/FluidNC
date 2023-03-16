// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	Cartesian.h

	This is a kinematic system for where the motors operate in the cartesian space.
*/

#include "GenericCartesian.h"

namespace Kinematics {
    class SkewAxis  : public Configuration::Configurable {
        uint _axisIdx;
    public:
        SkewAxis( int currentAxis ) : _axisIdx( currentAxis ) { _x[5] = _x[4] = _x[3] = _x[2] = _x[1] = _x[0] = 0.0f; };

        float    _dist     = 10.0f;
        float    _x[ 6 ];

        // Configuration system helpers:
        void validate() override {};
        void afterParse() override {};
        void group(Configuration::HandlerBase& handler) override;
        void init();

        void getRow( const uint count, float* buf );
    };


    class Skewed : public GenericCartesian {
        static constexpr const char* _names = "xyzabc";
        uint _numberAxis;

    public:
        Skewed();

        Skewed(const Skewed&)            = delete;
        Skewed(Skewed&&)                 = delete;
        Skewed& operator=(const Skewed&) = delete;
        Skewed& operator=(Skewed&&)      = delete;

        // Kinematic Interface
        virtual void init() override;

        // Configuration handlers:
        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;
        void validate() override;

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "Skew corrected Cartesian"; }

    protected:
        SkewAxis* _axis[6];
//        float _buffer[6];

        ~Skewed() {}
    };
}  //  namespace Kinematics
