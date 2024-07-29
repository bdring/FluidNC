// Copyright (c) 2021 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
	Midtbot.h

	Midtbot is simply a CoreXY with compensation for the moving motors

*/

#include "Kinematics.h"
#include "CoreXY.h"

namespace Kinematics {
    class Midtbot : public CoreXY {
    public:
        Midtbot(const char* name) : CoreXY(name) {}

        Midtbot(const Midtbot&)            = delete;
        Midtbot(Midtbot&&)                 = delete;
        Midtbot& operator=(const Midtbot&) = delete;
        Midtbot& operator=(Midtbot&&)      = delete;

        // Kinematic Interface

        void init() override;
        void group(Configuration::HandlerBase& handler) override;

        ~Midtbot() {}
    };
}  //  namespace Kinematics
