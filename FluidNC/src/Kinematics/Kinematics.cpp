// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Kinematics.h"

#include "Config.h"
#include "Cartesian.h"

namespace Kinematics {
    const char* no_system = "No kinematic system";

    void Kinematics::constrain_jog(float* target, plan_line_data_t* pl_data, float* position) {
        Assert(_system != nullptr, no_system);
        return _system->constrain_jog(target, pl_data, position);
    }

    bool Kinematics::invalid_line(float* target) {
        Assert(_system != nullptr, no_system);
        return _system->invalid_line(target);
    }

    bool Kinematics::invalid_arc(float*            target,
                                 plan_line_data_t* pl_data,
                                 float*            position,
                                 float             center[3],
                                 float             radius,
                                 axis_t            caxes[3],
                                 bool              is_clockwise_arc,
                                 uint32_t          rotations) {
        Assert(_system != nullptr, no_system);
        return _system->invalid_arc(target, pl_data, position, center, radius, caxes, is_clockwise_arc, rotations);
    }

    bool Kinematics::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        Assert(_system != nullptr, no_system);
        return _system->cartesian_to_motors(target, pl_data, position);
    }

    void Kinematics::motors_to_cartesian(float* cartesian, float* motors, axis_t n_axis) {
        Assert(_system != nullptr, no_system);
        return _system->motors_to_cartesian(cartesian, motors, n_axis);
    }

    bool Kinematics::canHome(AxisMask axisMask) {
        Assert(_system != nullptr, no_system);
        return _system->canHome(axisMask);
    }

    bool Kinematics::kinematics_homing(AxisMask axisMask) {
        Assert(_system != nullptr, no_system);
        return _system->kinematics_homing(axisMask);
    }

    void Kinematics::releaseMotors(AxisMask axisMask, MotorMask motors) {
        Assert(_system != nullptr, no_system);
        _system->releaseMotors(axisMask, motors);
    }

    bool Kinematics::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        Assert(_system != nullptr, no_system);
        return _system->limitReached(axisMask, motors, limited);
    }

    bool Kinematics::transform_cartesian_to_motors(float* motors, float* cartesian) {
        Assert(_system != nullptr, no_system);
        return _system->transform_cartesian_to_motors(motors, cartesian);
    }

    float Kinematics::min_motor_pos(axis_t axis) {
        Assert(_system != nullptr, no_system);
        return _system->min_motor_pos(axis);
    }

    float Kinematics::max_motor_pos(axis_t axis) {
        Assert(_system != nullptr, no_system);
        return _system->max_motor_pos(axis);
    }

    void Kinematics::homing_move(AxisMask axes, MotorMask motors, Machine::Homing::Phase phase, uint32_t settling_ms) {
        Assert(_system != nullptr, no_system);
        return _system->homing_move(axes, motors, phase, settling_ms);
    }

    void Kinematics::set_homed_mpos(float* mpos) {
        Assert(_system != nullptr, no_system);
        return _system->set_homed_mpos(mpos);
    }

    void Kinematics::group(Configuration::HandlerBase& handler) {
        ::Kinematics::KinematicsFactory::factory(handler, _system);
    }

    void Kinematics::afterParse() {
        if (_system == nullptr) {
            _system = new ::Kinematics::Cartesian("Cartesian");
        }
    }

    void Kinematics::init() {
        Assert(_system != nullptr, no_system);
        _system->init();
    }

    void Kinematics::init_position() {
        Assert(_system != nullptr, no_system);
        _system->init_position();
    }

    Kinematics::~Kinematics() {
        delete _system;
    }
};
