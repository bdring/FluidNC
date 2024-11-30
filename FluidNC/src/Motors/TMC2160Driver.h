// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TMC5160Driver.h"

namespace MotorDrivers {
    class TMC2160Driver : public TMC5160Driver {
    public:
        TMC2160Driver(const char* name) : TMC5160Driver(name) {}
    };
}
