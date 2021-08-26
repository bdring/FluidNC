// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
    This is a base class for servo-type motors - ones that autonomously
    move to a specified position, instead of being moved incrementally
    by stepping.  Specific kinds of servo motors inherit from it.
*/

#include "MotorDriver.h"

namespace MotorDrivers {
    class Servo : public MotorDriver {
    public:
        int _timer_ms = 75;

        Servo();
#if 0
        // Overrides for inherited methods
        void init() override;
        void read_settings() override;
        bool set_homing_mode(bool isHoming) override;
        void IRAM_ATTR set_disable(bool disable) override;
#endif
        virtual void update() = 0;  // This must be implemented by derived classes
        void         group(Configuration::HandlerBase& handler) override { handler.item("timer_ms", _timer_ms); }

    protected:
        // Start the servo update task.  Each derived subclass instance calls this
        // during init(), which happens after all objects have been constructed.
        // startUpdateTask(ms) finds the smallest update interval among all
        // the calls, and starts the task on the final call.
        void startUpdateTask(int ms);

    private:
        // Linked list of servo instances, used by the servo task
        static Servo* List;
        Servo*        link;
        static void   updateTask(void*);
    };
}
