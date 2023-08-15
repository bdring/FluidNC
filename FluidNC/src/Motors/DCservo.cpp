// Copyright (c) 2023 -	Bar Smith
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    DCservo.cpp

    This allows a DC servo to be used like any other motor.

*/

#include "DCservo.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"   // mpos_to_steps() etc
#include "../Limits.h"   // limitsMinPosition
#include "../Planner.h"  // plan_sync_position()

#include <cstdarg>
#include <cmath>

namespace MotorDrivers {

    DCservo::DCservo() {}

    void DCservo::init() {

        printf("DC Servo Init Ran\n");

        config_message();  // print the config

        startUpdateTask(_timer_ms);

        log_info("DC Servo Init Ran");
        printf("DC Servo Init Ran\n");
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<DCservo> registration("dc_servo");
    }
}
