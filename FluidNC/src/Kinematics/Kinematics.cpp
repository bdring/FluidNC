// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Kinematics.h"

#include "src/Config.h"
#include "Cartesian.h"

namespace Kinematics {
    bool Kinematics::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->cartesian_to_motors(target, pl_data, position);
    }

    void Kinematics::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->motors_to_cartesian(cartesian, motors, n_axis);
    }

    bool Kinematics::canHome(AxisMask axisMask) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->canHome(axisMask);
    }

    bool Kinematics::homingMove(
        AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate, uint32_t& settle_ms) {
        Assert(_system != nullptr, "No kinematic system");
        return _system->homingMove(axisMask, motors, phase, target, rate, settle_ms);
    }

    bool Kinematics::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        Assert(_system != nullptr, "No kinematics system.");
        return _system->limitReached(axisMask, motors, limited);
    }

    void Kinematics::group(Configuration::HandlerBase& handler) { ::Kinematics::KinematicsFactory::factory(handler, _system); }

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
