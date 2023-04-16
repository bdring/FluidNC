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
#include "../Machine/MachineConfig.h"

#include <atomic>
#include <freertos/task.h>  // portTICK_PERIOD_MS, vTaskDelay

namespace MotorDrivers {

    Servo::Servo() : MotorDriver() {}

    void Servo::update_servo(TimerHandle_t object) {
        Servo* servo = static_cast<Servo*>(object);
        servo->update();
    }

    void Servo::schedule_update(Servo* object, int interval) {
        xTimerCreate("",
                     interval,
                     true,  // auto reload
                     (TimerHandle_t)object,
                     update_servo);
    }
}
