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
        _negLimitPin = new LimitPin(_negPin, _axis, _motorNum, -1, _hardLimits);
        _posLimitPin = new LimitPin(_posPin, _axis, _motorNum, 1, _hardLimits);
        _allLimitPin = new LimitPin(_allPin, _axis, _motorNum, 0, _hardLimits);
        handler.item("limit_neg", _negPin);
        handler.item("limit_pos", _posPin);
        handler.item("limit_all", _allPin);
        handler.item("hard_limits", _hardLimits);
        handler.item("pulloff", _pulloff);
        MotorDrivers::MotorFactory::factory(handler, _driver);
    }

    void Motor::afterParse() {
        if (_driver == nullptr) {
            _driver = new MotorDrivers::Nullmotor();
        }
    }

    void Motor::init() {
        if (strcmp(_driver->name(), "null_motor") != 0) {
            set_bitnum(Machine::Axes::motorMask, _axis + 16 * _motorNum);
        }
        _driver->init();

        _negLimitPin->init();
        _posLimitPin->init();
        _allLimitPin->init();
    }

    // tru if there is at least one switch for this motor
    bool Motor::hasSwitches() { return (_negPin.defined() || _posPin.defined() || _allPin.defined()); }

    // Used when a single switch input is wired to 2 axes.
    void Motor::makeDualSwitches() {
        _negLimitPin->makeDualMask();
        _posLimitPin->makeDualMask();
        _allLimitPin->makeDualMask();
    }

    Motor::~Motor() { delete _driver; }
}
