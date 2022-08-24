// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "LimitPin.h"

namespace MotorDrivers {
    class MotorDriver;
}

namespace Machine {
    class Endstops;
}

namespace Machine {
    class Motor : public Configuration::Configurable {
        LimitPin* _negLimitPin;
        LimitPin* _posLimitPin;
        LimitPin* _allLimitPin;

        int _axis;
        int _motorNum;

    public:
        Motor(int axis, int motorNum) : _axis(axis), _motorNum(motorNum) {}

        MotorDrivers::MotorDriver* _driver  = nullptr;
        float                      _pulloff = 1.0f;  // mm

        Pin  _negPin;
        Pin  _posPin;
        Pin  _allPin;
        bool _hardLimits = false;

        int32_t _steps   = 0;
        bool    _limited = false;  // _limited is set by the LimitPin ISR
        bool    _blocked = false;  // _blocked is used during asymmetric homing pulloff

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        bool hasSwitches();
        bool isReal();
        void makeDualSwitches();
        void limitOtherAxis(int axis);
        void init();
        void config_motor();
        void step(bool reverse);
        void unstep();
        void block() { _blocked = true; }
        void unblock() { _blocked = false; }
        void unlimit() { _limited = false; }
        ~Motor();
    };
}
