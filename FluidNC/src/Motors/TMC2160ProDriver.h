// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "TMC5160ProDriver.h"
#include "Pin.h"
#include "PinMapper.h"

#include <cstdint>

const float TMC2160_RSENSE_DEFAULT = 0.050f;  // Ref only, not used

namespace MotorDrivers {

    class TMC2160Driver : public TMC5160ProDriver {
    public:
        TMC2160Driver(const char* name) : TMC5160ProDriver(name) {}

    private:
    };
}
