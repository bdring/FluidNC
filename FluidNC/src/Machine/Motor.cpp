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
        // @config limit_neg_pin
        // @default NO_PIN
        // @pin_attributes input
        // Limit switch on the negative-direction end of this motor's travel.
        handler.item("limit_neg_pin", _negLimitPin);

        // @config limit_pos_pin
        // @default NO_PIN
        // @pin_attributes input
        // Limit switch on the positive-direction end of this motor's travel -- often just
        // beyond the axis's max_travel_mm.
        handler.item("limit_pos_pin", _posLimitPin);

        // @config limit_all_pin
        // @default NO_PIN
        // @pin_attributes input
        // A single switch wired to both ends of travel. Mutually exclusive with
        // limit_neg_pin/limit_pos_pin -- don't specify this alongside either of them.
        // Since FluidNC can't tell which end triggered it, it also can't tell which
        // direction to move to clear it, so a limit_all_pin switch must be manually
        // cleared before homing.
        handler.item("limit_all_pin", _allLimitPin);

        // @config hard_limits
        // @default false
        // @tuning typical
        // Treats the switches above as hard limits: activating one immediately stops all
        // motion. Position is considered lost, so rehoming is required afterward.
        handler.item("hard_limits", _hardLimits);

        // @config pulloff_mm
        // @default 1.0
        // Distance to back off a triggered switch during homing, for this motor. Must be
        // greater than however far the axis can still travel after the switch first
        // activates, so the switch reliably clears every time.
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

    void Motor::rearmSwitches() {
        if (_negLimitPin.defined()) {
            _negLimitPin.rearm();
        }
        if (_posLimitPin.defined()) {
            _posLimitPin.rearm();
        }
        if (_allLimitPin.defined()) {
            _allLimitPin.rearm();
        }
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
