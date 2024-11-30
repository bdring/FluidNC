// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TMC2160Driver.h"
#include "../Machine/MachineConfig.h"

namespace MotorDrivers {
    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC2160Driver> registration("tmc_2160");
    }
}
