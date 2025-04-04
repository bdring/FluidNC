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
        LimitPin _negLimitPin;
        LimitPin _posLimitPin;
        LimitPin _allLimitPin;

        int _axis;
        int _motorNum;

    public:
        Motor(int axis, int motorNum);

        MotorDrivers::MotorDriver* _driver  = nullptr;
        float                      _pulloff = 1.0f;  // mm

        bool _hardLimits = false;

        // Configuration system helpers:
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        bool hasSwitches();
        bool isReal();
        void makeDualSwitches();
        void limitOtherAxis(int axis);
        void init();
        void config_motor();
        ~Motor();
    };
}
