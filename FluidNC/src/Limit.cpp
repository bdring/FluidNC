// Copyright (c) 2012 - 2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Limit.h"

#include "Machine/MachineConfig.h"
#include "MotionControl.h"  // mc_critical
#include "System.h"         // sys.*
#include "Protocol.h"       // protocol_execute_realtime
#include "Platform.h"       // WEAK_LINK
#include "Machine/Axis.h"
#include "GCode.h"  // gc_last_line
#include "Job.h"    // Job::channel()

#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>  // fence

QueueHandle_t limit_sw_queue;  // used by limit switch debouncing

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

// Called only from Kinematics canHome() methods, hence from states allowing homing
bool ambiguousLimit() {
    if (Machine::Axes::posLimitMask & Machine::Axes::negLimitMask) {
        mc_critical(ExecAlarm::HomingAmbiguousSwitch);
        return true;
    }
    return false;
}

bool soft_limit = false;

// Logs the commanded mpos, the gcode line that produced it, and the job location
// (file+line, or N-word), shared by both flavors of limit_error() below. target is
// the full N-axis position that was commanded, if known.
static void log_limit_context(float* target, plan_line_data_t* pl_data) {
    // A clustered move (e.g. dynamic laser power) reports the original un-split
    // command endpoint rather than the interior segment endpoint that was actually
    // being planned when the violation was detected.
    if (pl_data && pl_data->report_target) {
        target = pl_data->report_target;
    }

    if (target) {
        LogStream ls(MsgLevelInfo, "[MSG:INFO: ");
        ls << "Commanded mpos:";
        auto n_axis = Machine::Axes::_numberAxis;
        for (axis_t a = X_AXIS; a < n_axis; a++) {
            ls << " " << Machine::Axes::axisName(a) << target[a];
        }
    }

    if (gc_last_line && gc_last_line[0]) {
        log_info("GCode line: " << gc_last_line);
    }

    Channel* job = Job::channel();
    if (job) {
        // Running from a file or macro; report where in the job this occurred.
        log_info("In " << job->name() << " at line " << job->lineNumber());
    } else if (pl_data && pl_data->line_number) {
        log_info("At N" << pl_data->line_number);
    }
}

// Performs a soft limit check. Called from mcline() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
// NOTE: Used by jogging to limit travel within soft-limit volume.
void limit_error(axis_t axis, float coordinate, float* target, plan_line_data_t* pl_data) {
    log_info("Soft limit exceeded on " << Machine::Axes::axisName(axis) << " axis: target " << coordinate << " mm, limit ["
                                        << limitsMinPosition(axis) << ", " << limitsMaxPosition(axis) << "] mm");

    log_limit_context(target, pl_data);

    limit_error();
}

// Reports a soft limit failure that isn't tied to a single axis/bound, e.g. a
// commanded position that inverse kinematics cannot reach at all.
void limit_error(float* target, plan_line_data_t* pl_data) {
    log_info("Soft limit: commanded position is outside the reachable workspace");

    log_limit_context(target, pl_data);

    limit_error();
}

void limit_error() {
    soft_limit = true;
    // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within
    // workspace volume so just come to a controlled stop so position is not lost. When complete
    // enter alarm mode.
    protocol_buffer_synchronize();
    if (state_is(State::Cycle)) {
        protocol_send_event(&feedHoldEvent);
        do {
            protocol_execute_realtime();
            if (sys.abort()) {
                return;
            }
        } while (!state_is(State::Idle));
    }

    mc_critical(ExecAlarm::SoftLimit);
}

float limitsMaxPosition(axis_t axis) {
    auto axisConfig = Axes::_axis[axis];
    auto homing     = axisConfig->_homing;
    auto mpos       = homing ? homing->_mpos : 0;
    auto maxtravel  = axisConfig->_maxTravel;
    auto slop       = 1.0F / axisConfig->_stepsPerMm;

    return ((!homing || homing->_positiveDirection) ? mpos : mpos + maxtravel) + slop;
}

float limitsMinPosition(axis_t axis) {
    auto axisConfig = Axes::_axis[axis];
    auto homing     = axisConfig->_homing;
    auto mpos       = homing ? homing->_mpos : 0;
    auto maxtravel  = axisConfig->_maxTravel;
    auto slop       = 1.0F / axisConfig->_stepsPerMm;

    return ((!homing || homing->_positiveDirection) ? mpos - maxtravel : mpos) - slop;
}
