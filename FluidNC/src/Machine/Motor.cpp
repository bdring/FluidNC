// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Motor.h"

#include "Config.h"
#include "Motors/MotorDriver.h"
#include "Motors/NullMotor.h"
#include "Axes.h"

namespace Machine {
    Motor::Motor(axis_t axis, motor_t motorNum) :
        _axis(axis), _motorNum(motorNum), _negLimitPin(axis, motorNum, -1, _hardLimits), _posLimitPin(axis, motorNum, 1, _hardLimits),
        _allLimitPin(axis, motorNum, 0, _hardLimits) {}

    void Motor::group(Configuration::HandlerBase& handler) {
        handler.item("limit_neg_pin", _negLimitPin);
        handler.item("limit_pos_pin", _posLimitPin);
        handler.item("limit_all_pin", _allLimitPin);
        handler.item("hard_limits", _hardLimits);
        handler.item("pulloff_mm", _pulloff, 0.1, 100000.0);
        MotorDrivers::MotorFactory::factory(handler, _driver);
    }

    void Motor::afterParse() {
        if (_driver == nullptr) {
            _driver = new MotorDrivers::Nullmotor("null_motor");
        }
    }

    void Motor::init() {
        if (strcmp(_driver->name(), "null_motor") != 0) {
            set_bitnum(Machine::Axes::motorMask, Machine::Axes::motor_bit(_axis, _motorNum));
        }
        _driver->init();

        _negLimitPin.init();
        _posLimitPin.init();
        _allLimitPin.init();
    }

    void Motor::config_motor() {
        if (_driver != nullptr) {
            _driver->config_motor();
        }
    }

    // true if there is at least one switch for this motor
    bool Motor::hasSwitches() {
        return (_negLimitPin.defined() || _posLimitPin.defined() || _allLimitPin.defined());
    }

    // Used when a single switch input is wired to 2 axes.
    void Motor::makeDualSwitches() {
        _negLimitPin.makeDualMask();
        _posLimitPin.makeDualMask();
        _allLimitPin.makeDualMask();
    }

    // Used for CoreXY when one limit switch should stop multiple motors
    void Motor::limitOtherAxis(axis_t axis) {
        _negLimitPin.setExtraMotorLimit(axis, _motorNum);
        _posLimitPin.setExtraMotorLimit(axis, _motorNum);
        _allLimitPin.setExtraMotorLimit(axis, _motorNum);
    }

    bool Motor::isReal() {
        return _driver->isReal();
    }

    bool Motor::can_home() {
        return (_driver->can_self_home() || hasSwitches());
    }

    // Use true to check positive and false to check negative homing directions
    bool Motor::supports_homing_dir(bool positive) {
        if (_driver->can_self_home() || _allLimitPin.defined()) {
            return true;
        }
        return positive ? _posLimitPin.defined() : _negLimitPin.defined();
    }

    Motor::~Motor() {
        delete _driver;
    }
}
