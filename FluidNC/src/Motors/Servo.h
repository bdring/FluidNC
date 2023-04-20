// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>  // TimerHandle_t

/*
    This is a base class for servo-type motors - ones that autonomously
    move to a specified position, instead of being moved incrementally
    by stepping.  Specific kinds of servo motors inherit from it.
*/

#include "MotorDriver.h"

namespace MotorDrivers {
    class Servo : public MotorDriver {
    public:
        Servo();

        virtual void update() = 0;  // This must be implemented by derived classes
        void         group(Configuration::HandlerBase& handler) override {}

        virtual const char* name() = 0;  // This must be implemented by derived classes

    protected:
        static void update_servo(TimerHandle_t timer);
        static void schedule_update(Servo* object, int interval);
    };
}
