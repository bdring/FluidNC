// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Motor.h"

#include "../Config.h"
#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"
#include "Axes.h"

namespace Machine {
    Motor::Motor(int axis, int motorNum) : _axis(axis), _motorNum(motorNum) {}

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
        log_debug("Initializing motor / limits...");

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

        unblock();
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

    // Used for CoreXY when one limit switch should stop multiple motors
    void Motor::limitOtherAxis(int axis) {
        _negLimitPin->setExtraMotorLimit(axis, _motorNum);
        _posLimitPin->setExtraMotorLimit(axis, _motorNum);
        _allLimitPin->setExtraMotorLimit(axis, _motorNum);
    }

    bool Motor::isReal() { return _driver->isReal(); }

    void IRAM_ATTR Motor::step(bool reverse) {
        // Skip steps based on limit pins
        // _blocked is for asymmetric pulloff
        // _limited is for limit pins
        if (_blocked || _limited) {
            return;
        }
        _driver->step();
        _steps += reverse ? -1 : 1;
    }

    void IRAM_ATTR Motor::unstep() { _driver->unstep(); }

    Motor::~Motor() { delete _driver; }
}
