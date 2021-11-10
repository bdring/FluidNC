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
        Midtbot() = default;

        Midtbot(const Midtbot&) = delete;
        Midtbot(Midtbot&&)      = delete;
        Midtbot& operator=(const Midtbot&) = delete;
        Midtbot& operator=(Midtbot&&) = delete;

        // Kinematic Interface

        void init() override;
        void group(Configuration::HandlerBase& handler) override;

        // Name of the configurable. Must match the name registered in the cpp file.
        const char* name() const override { return "midtbot"; }

        ~Midtbot() {}
    };
}  //  namespace Kinematics
