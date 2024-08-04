// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	Skewed.h

	This is a kinematic system for where the motors operate in the cartesian space. 
    But unlike Cartesian kinematic system, it adds ability to tweak skew correction, 
    for cases when physical geometry of CNC machine is not ideal.
*/

#include "GenericCartesian.h"

namespace Kinematics {
    class SkewAxis  : public Configuration::Configurable {
    public:
        SkewAxis() { _offsets[5] = _offsets[4] = _offsets[3] = _offsets[2] = _offsets[1] = _offsets[0] = 0.0f; };

        float    _dist     = 10.0f;
        float    _offsets[ 6 ];

        // Configuration system helpers:
        void validate() override {};
        void afterParse() override {};
        void group(Configuration::HandlerBase& handler) override;

        void init();
    };


    class Skewed : public GenericCartesian {
        uint _numberSkewAxis;

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
        SkewAxis* _skewAxis[6];
//        float _buffer[6];

        ~Skewed() {}
    };
}  //  namespace Kinematics
