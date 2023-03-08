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

    void Kinematics::releaseMotors(AxisMask axisMask, MotorMask motors) {
        Assert(_system != nullptr, "No kinematic system");
        _system->releaseMotors(axisMask, motors);
    }

    bool Kinematics::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        Assert(_system != nullptr, "No kinematics system.");
        return _system->limitReached(axisMask, motors, limited);
    }

    void Kinematics::transform_cartesian_to_motors(float* motors, float* cartesian) {
        Assert(_system != nullptr, "No kinematics system.");
        return _system->transform_cartesian_to_motors(motors, cartesian);
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

    void Kinematics::init_position() {
        Assert(_system != nullptr, "init_position: Kinematics system missing.");
        _system->init_position();
    }

    Kinematics::~Kinematics() { delete _system; }
};
