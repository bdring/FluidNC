// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    This is a base class for servo-type motors - ones that autonomously
    move to a specified position, instead of being moved incrementally
    by stepping.  Specific kinds of servo motors inherit from it.

    The servo's travel will be mapped against the axis with $X/MaxTravel

    The rotation can be inverted with by $Stepper/DirInvert

    Homing simply sets the axis Mpos to the endpoint as determined by $Homing/DirInvert

    Calibration is part of the setting (TBD) fixed at 1.00 now
*/

#include "Servo.h"
#include "Machine/MachineConfig.h"

#include <atomic>

namespace MotorDrivers {
    void Servo::update_servo(TimerHandle_t timer) {
        Servo* servo = static_cast<Servo*>(pvTimerGetTimerID(timer));
        servo->update();
    }

    void Servo::schedule_update(Servo* object, uint32_t interval) {
        auto timer = xTimerCreate("",
                                  interval,
                                  true,  // auto reload
                                  (void*)object,
                                  update_servo);
        if (!timer) {
            log_error("Failed to create timer for " << object->name());
            return;
        }
        if (xTimerStart(timer, 0) == pdFAIL) {
            log_error("Failed to start timer for " << object->name());
        }
        log_info("    Update timer for " << object->name() << " at " << interval << " ms");
    }
}
