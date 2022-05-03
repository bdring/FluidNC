// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Motor.h"

#include "../Config.h"
#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"
#include "Axes.h"

namespace Machine {
    void Motor::group(Configuration::HandlerBase& handler) {
        handler.item("limit_neg_pin", _negPin);
        handler.item("limit_pos_pin", _posPin);
        handler.item("limit_all_pin", _allPin);
        handler.item("hard_limits", _hardLimits);
        handler.item("pulloff_mm", _pulloff, 0.1, 100000.0);
        MotorDrivers::MotorFactory::factory(handler, _driver);
    }

    void Motor::afterParse() {
        if (_driver == nullptr) {
            _driver = new MotorDrivers::Nullmotor();
        }
    }

    void Motor::init() {
        if (strcmp(_driver->name(), "null_motor") != 0) {
            set_bitnum(Machine::Axes::motorMask, Machine::Axes::motor_bit(_axis, _motorNum));
        }
        _driver->init();

        _negLimitPin = new LimitPin(_negPin, _axis, _motorNum, -1, _hardLimits, _limited);
        _posLimitPin = new LimitPin(_posPin, _axis, _motorNum, 1, _hardLimits, _limited);
        _allLimitPin = new LimitPin(_allPin, _axis, _motorNum, 0, _hardLimits, _limited);

        _negLimitPin->init();
        _posLimitPin->init();
        _allLimitPin->init();
    }

    void Motor::config_motor() {
        if (_driver != nullptr) {
            _driver->config_motor();
        }
    }

    // true if there is at least one switch for this motor
    bool Motor::hasSwitches() { return (_negPin.defined() || _posPin.defined() || _allPin.defined()); }

    // Used when a single switch input is wired to 2 axes.
    void Motor::makeDualSwitches() {
        _negLimitPin->makeDualMask();
        _posLimitPin->makeDualMask();
        _allLimitPin->makeDualMask();
    }

    bool Motor::isReal() { return _driver->isReal(); }

    void Motor::step(bool reverse) {
        // Skip steps based on limit pins
        if (_limited && (Homing::_approach || (sys.state != State::Homing && _hardLimits))) {
            return;
        }
        _driver->step();
        _steps += reverse ? -1 : 1;
    }

    void Motor::unstep() { _driver->unstep(); }

    Motor::~Motor() { delete _driver; }
}
