// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a fake motor that does nothing.
*/

#include "NullMotor.h"

namespace MotorDrivers {
    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<Nullmotor> registration("null_motor");
    }
}
