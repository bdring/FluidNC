// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Kinematics.h"

#include "../Config.h"
#include "Cartesian.h"

namespace Kinematics {
    bool Kinematics::kinematics_homing(AxisMask cycle_mask) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->kinematics_homing(cycle_mask);
    }

    void Kinematics::kinematics_post_homing() {
        Assert(_system != nullptr, "No kinematic system");
        return _system->kinematics_post_homing();
    }

    bool Kinematics::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->cartesian_to_motors(target, pl_data, position);
    }

    void Kinematics::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->motors_to_cartesian(cartesian, motors, n_axis);
    }

    void Kinematics::group(Configuration::HandlerBase& handler) {
        ::Kinematics::KinematicsFactory::factory(handler, _system);
        Assert(_system != nullptr, "No kinematics system.");
    }

    void Kinematics::afterParse() {
        if (_system == nullptr) {
            _system = new ::Kinematics::Cartesian();
        }
    }

    void Kinematics::init() {
        Assert(_system != nullptr, "init: Kinematics system missing.");
        _system->init();
    }

    Kinematics::~Kinematics() { delete _system; }
};
