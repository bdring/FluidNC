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
        Servo(const char* name) : MotorDriver(name) {}

        virtual void update() = 0;  // This must be implemented by derived classes
        void         group(Configuration::HandlerBase& handler) override {}

        bool can_self_home() override { return true; }

    protected:
        static void update_servo(TimerHandle_t timer);
        static void schedule_update(Servo* object, uint32_t interval);

        // Common to every concrete Servo driver (RcServo, Solenoid, Dynamixel2):
        // which axis this instance is driving, and whether it's currently
        // disabled. Deliberately NOT _has_errors -- that one has genuinely
        // different semantics per driver (e.g. Dynamixel2's is a single
        // static flag shared across every instance on its UART bus, since
        // one erroring servo likely means the whole bus is broken; a plain
        // per-instance member here would silently change that).
        axis_t _axis     = INVALID_AXIS;
        bool   _disabled = true;
    };
}
