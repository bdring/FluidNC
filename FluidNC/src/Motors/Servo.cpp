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
    Servo* Servo::List = NULL;

    Servo::Servo() : MotorDriver() {
        link = List;
        List = this;
    }

    void Servo::startUpdateTask(int ms) {
        if (_timer_ms == 0 || ms < _timer_ms) {
            _timer_ms = ms;
        }
        //log_info("Servo Update Task Started");
        if (this == List) {
            xTaskCreatePinnedToCore(updateTask,         // task
                                    "servoUpdateTask",  // name for task
                                    4096,               // size of task stack
                                    (void*)&_timer_ms,  // parameters
                                    1,                  // priority
                                    NULL,               // handle
                                    SUPPORT_TASK_CORE   // core
            );
        }
    }

    void Servo::updateTask(void* pvParameters) {
        TickType_t       xLastWakeTime;
        const TickType_t xUpdate = *static_cast<TickType_t*>(pvParameters) / portTICK_PERIOD_MS;  // in ticks (typically ms)
        auto             n_axis  = config->_axes->_numberAxis;

        xLastWakeTime = xTaskGetTickCount();  // Initialise the xLastWakeTime variable with the current time.
        vTaskDelay(2000);                     // initial delay
        while (true) {                        // don't ever return from this or the task dies
            std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
            //log_info("Servo update");
            for (Servo* p = List; p; p = p->link) {
                p->update();
            }

            vTaskDelayUntil(&xLastWakeTime, xUpdate);

            static UBaseType_t uxHighWaterMark = 0;
#ifdef DEBUG_TASK_STACK
            reportTaskStackSize(uxHighWaterMark);
#endif
        }
    }
}
