// Copyright (c) 2012 - 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Limits.h"

#include "Machine/MachineConfig.h"
#include "MotionControl.h"  // mc_reset
#include "System.h"         // sys.*
#include "Protocol.h"       // protocol_execute_realtime
#include "Platform.h"       // WEAK_LINK

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>             // fence

xQueueHandle limit_sw_queue;  // used by limit switch debouncing

void limits_init() {
#ifdef LATER  // We need to rethink debouncing
    if (Machine::Axes::limitMask) {
        if (limit_sw_queue == NULL && config->_softwareDebounceMs != 0) {
            // setup task used for debouncing
            if (limit_sw_queue == NULL) {
                limit_sw_queue = xQueueCreate(10, sizeof(int));
                xTaskCreate(limitCheckTask,
                            "limitCheckTask",
                            2048,
                            NULL,
                            5,  // priority
                            NULL);
            }
        }
    }
#endif
}

// Returns limit state as a bit-wise uint32 variable. Each bit indicates an axis limit, where
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. Z_AXIS is bitnum_to_mask(2), and Y_AXIS is bitnum_to_mask(1).
// The lower 16 bits are used for motor0 and the upper 16 bits are used for motor1 switches
MotorMask limits_get_state() {
    return Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;
}

bool ambiguousLimit() {
    if (Machine::Axes::posLimitMask & Machine::Axes::negLimitMask) {
        mc_reset();  // Issue system reset and ensure spindle and coolant are shutdown.
        rtAlarm = ExecAlarm::HardLimit;
        return true;
    }
    return false;
}

bool soft_limit = false;

// Constrain the coordinates to stay within the soft limit envelope
void constrainToSoftLimits(float* cartesian) {
    auto axes   = config->_axes;
    auto n_axis = config->_axes->_numberAxis;

    float*    current_position = get_mpos();
    MotorMask lim_pin_state    = limits_get_state();

    for (int axis = 0; axis < n_axis; axis++) {
        auto axisSetting = axes->_axis[axis];
        // If the axis is moving from the current location and soft limits are on.
        if (axisSetting->_softLimits && cartesian[axis] != current_position[axis]) {
            // When outside the axis range, only small nudges to clear switches are allowed
            bool move_positive = cartesian[axis] > current_position[axis];
            if ((!move_positive && (current_position[axis] < limitsMinPosition(axis))) ||
                (move_positive && (current_position[axis] > limitsMaxPosition(axis)))) {
                // only allow a nudge if a switch is active
                if (bitnum_is_false(lim_pin_state, Machine::Axes::motor_bit(axis, 0)) &&
                    bitnum_is_false(lim_pin_state, Machine::Axes::motor_bit(axis, 1))) {
                    cartesian[axis] = current_position[axis];  // cancel the move on this axis
                    log_debug("Soft limit violation on " << Machine::Axes::_names[axis]);
                    continue;
                }
                float jog_dist = cartesian[axis] - current_position[axis];

                MotorMask axisMotors = Machine::Axes::axes_to_motors(1 << axis);
                bool      posLimited = bits_are_true(Machine::Axes::posLimitMask, axisMotors);
                bool      negLimited = bits_are_true(Machine::Axes::negLimitMask, axisMotors);

                // if jog is positive and only the positive switch is active, then kill the move
                // if jog is negative and only the negative switch is active, then kill the move
                if (posLimited != negLimited) {                    // XOR, because ambiguous (both) is OK
                    if ((negLimited && (jog_dist < 0)) || (posLimited && (jog_dist > 0))) {
                        cartesian[axis] = current_position[axis];  // cancel the move on this axis
                        log_debug("Jog into active switch blocked on " << Machine::Axes::_names[axis]);
                        continue;
                    }
                }

                auto nudge_max = axisSetting->_motors[0]->_pulloff;
                if (abs(jog_dist) > nudge_max) {
                    cartesian[axis] = (jog_dist >= 0) ? current_position[axis] + nudge_max : current_position[axis] + nudge_max;
                    log_debug("Jog amount limited when outside soft limits")
                }
                continue;
            }

            if (cartesian[axis] < limitsMinPosition(axis)) {
                cartesian[axis] = limitsMinPosition(axis);
            } else if (cartesian[axis] > limitsMaxPosition(axis)) {
                cartesian[axis] = limitsMaxPosition(axis);
            } else {
                continue;
            }
            log_debug("Jog constrained to axis range");
        }
    }
}

// Performs a soft limit check. Called from mcline() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limits_soft_check(float* cartesian) {
    bool limit_error = false;

    auto axes   = config->_axes;
    auto n_axis = config->_axes->_numberAxis;

    for (int axis = 0; axis < n_axis; axis++) {
        if (axes->_axis[axis]->_softLimits && (cartesian[axis] < limitsMinPosition(axis) || cartesian[axis] > limitsMaxPosition(axis))) {
            log_info("Soft limit on " << Machine::Axes::_names[axis] << " target:" << cartesian[axis]);
            limit_error = true;
        }
    }

    if (limit_error) {
        soft_limit = true;
        // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within
        // workspace volume so just come to a controlled stop so position is not lost. When complete
        // enter alarm mode.
        if (sys.state == State::Cycle) {
            protocol_send_event(&feedHoldEvent);
            do {
                protocol_execute_realtime();
                if (sys.abort) {
                    return;
                }
            } while (sys.state != State::Idle);
        }
        log_debug("Soft limits");
        mc_reset();                      // Issue system reset and ensure spindle and coolant are shutdown.
        rtAlarm = ExecAlarm::SoftLimit;  // Indicate soft limit critical event
        protocol_execute_realtime();     // Execute to enter critical event loop and system abort
    }
}

#ifdef LATER  // We need to rethink debouncing
void limitCheckTask(void* pvParameters) {
    while (true) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings

        int evt;
        xQueueReceive(limit_sw_queue, &evt, portMAX_DELAY);            // block until receive queue
        vTaskDelay(config->_softwareDebounceMs / portTICK_PERIOD_MS);  // delay a while
        auto switch_state = limits_get_state();
        if (switch_state) {
            log_debug("Limit Switch State " << to_hex(switch_state));
            mc_reset();                      // Initiate system kill.
            rtAlarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
        }
        static UBaseType_t uxHighWaterMark = 0;
#    ifdef DEBUG_TASK_STACK
        reportTaskStackSize(uxHighWaterMark);
#    endif
    }
}
#endif

float limitsMaxPosition(size_t axis) {
    auto  axisConfig = config->_axes->_axis[axis];
    auto  homing     = axisConfig->_homing;
    float mpos       = (homing != nullptr) ? homing->_mpos : 0;
    auto  maxtravel  = axisConfig->_maxTravel;

    //return (homing == nullptr || homing->_positiveDirection) ? mpos + maxtravel : mpos;
    return (homing == nullptr || homing->_positiveDirection) ? mpos : mpos + maxtravel;
}

float limitsMinPosition(size_t axis) {
    auto  axisConfig = config->_axes->_axis[axis];
    auto  homing     = axisConfig->_homing;
    float mpos       = (homing != nullptr) ? homing->_mpos : 0;
    auto  maxtravel  = axisConfig->_maxTravel;

    //return (homing == nullptr || homing->_positiveDirection) ? mpos : mpos - maxtravel;
    return (homing == nullptr || homing->_positiveDirection) ? mpos - maxtravel : mpos;
}
