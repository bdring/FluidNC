// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MotionControl.h"

#include "Machine/MachineConfig.h"
#include "Machine/Homing.h"  // run_cycles
#include "Limits.h"          // limits_soft_check
#include "Report.h"          // report_over_counter
#include "Protocol.h"        // protocol_execute_realtime
#include "Planner.h"         // plan_reset, etc
#include "I2SOut.h"          // i2s_out_reset
#include "InputFile.h"       // infile
#include "Platform.h"        // WEAK_LINK

#include <cmath>

// M_PI is not defined in standard C/C++ but some compilers
// support it anyway.  The following suppresses Intellisense
// problem reports.
#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif

// mc_pl_data_inflight keeps track of a jog command sent to mc_move_motors() so we can cancel it.
// this is needed if a jogCancel comes along after we have already parsed a jog and it is in-flight.
static volatile void* mc_pl_data_inflight;  // holds a plan_line_data_t while mc_move_motors has taken ownership of a line motion

void mc_init() {
    mc_pl_data_inflight = NULL;
}

// Execute linear motor motion in absolute millimeter coordinates. Feed rate given in
// millimeters/second unless invert_feed_rate is true.
// Then the feed_rate means that the motion should be completed in (1 minute)/feed_rate time.
//
// NOTE: This operates in the motor space rather than cartesian space. If a cartesian linear motion
// is desired, please see mc_linear() which will translate from cartesian to motor operations via
// kinematics.
//
// NOTE: This is the primary gateway to the planner. All line motions, including arc line
// segments, must pass through this routine before being passed to the planner. The seperation of
// mc_linear and plan_buffer_line is done primarily to place non-planner-type functions from being
// in the planner and to let backlash compensation or canned cycle integration simple and direct.
// returns true if line was submitted to planner, or false if intentionally dropped.
bool mc_move_motors(float* target, plan_line_data_t* pl_data) {
    bool submitted_result = false;
    // store the plan data so it can be cancelled by the protocol system if needed
    mc_pl_data_inflight = pl_data;

    // If enabled, check for soft limit violations.
    bool hasSoftLimits = config->_axes->hasSoftLimits();
    if (hasSoftLimits) {
        // NOTE: Block jog state. Jogging is a special case and soft limits are handled independently.
        if (sys.state != State::Jog) {
            limits_soft_check(target);
        }
    }
    // If in check gcode mode, prevent motion by blocking planner. Soft limits still work.
    if (sys.state == State::CheckMode) {
        mc_pl_data_inflight = NULL;
        return submitted_result;  // Bail, if system abort.
    }
    // NOTE: Backlash compensation may be installed here. It will need direction info to track when
    // to insert a backlash line motion(s) before the intended line motion and will require its own
    // plan_check_full_buffer() and check for system abort loop. Also for position reporting
    // backlash steps will need to be also tracked, which will need to be kept at a system level.
    // There are likely some other things that will need to be tracked as well. However, we feel
    // that backlash compensation should NOT be handled by the firmware itself, because there are a myriad
    // of ways to implement it and can be effective or ineffective for different CNC machines. This
    // would be better handled by the interface as a post-processor task, where the original g-code
    // is translated and inserts backlash motions that best suits the machine.
    // NOTE: Perhaps as a middle-ground, all that needs to be sent is a flag or special command that
    // indicates to the firmware what is a backlash compensation motion, so that the move is executed
    // without updating the machine position values. Since the position values used by the g-code
    // parser and planner are separate from the system machine positions, this is doable.
    // If the buffer is full: good! That means we are well ahead of the robot.
    // Remain in this loop until there is room in the buffer.

    while (plan_check_full_buffer()) {
        protocol_auto_cycle_start();  // Auto-cycle start when buffer is full.

        // While we are waiting for room in the buffer, look for realtime
        // commands and other situations that could cause state changes.
        pollChannels();
        protocol_execute_realtime();
        if (sys.abort) {
            mc_pl_data_inflight = NULL;
            return submitted_result;  // Bail, if system abort.
        }
    }

    // Plan and queue motion into planner buffer
    if (mc_pl_data_inflight == pl_data) {
        plan_buffer_line(target, pl_data);
        submitted_result = true;
    }
    mc_pl_data_inflight = NULL;
    return submitted_result;
}

void mc_cancel_jog() {
    if (mc_pl_data_inflight != NULL && ((plan_line_data_t*)mc_pl_data_inflight)->is_jog) {
        mc_pl_data_inflight = NULL;
    }
}

// Execute linear motion in absolute millimeter coordinates. Feed rate given in millimeters/second
// unless invert_feed_rate is true. Then the feed_rate means that the motion should be completed in
// (1 minute)/feed_rate time.
bool mc_linear(float* target, plan_line_data_t* pl_data, float* position) {
    return config->_kinematics->cartesian_to_motors(target, pl_data, position);
}

// Execute an arc in offset mode format. position == current xyz, target == target xyz,
// offset == offset from current xyz, axis_X defines circle plane in tool space, axis_linear is
// the direction of helical travel, radius == circle radius, isclockwise boolean. Used
// for vector transformation direction.
// The arc is approximated by generating a huge number of tiny, linear segments. The chordal tolerance
// of each segment is configured in the arc_tolerance setting, which is defined to be the maximum normal
// distance from segment to the circle when the end points both lie on the circle.
void mc_arc(float*            target,
            plan_line_data_t* pl_data,
            float*            position,
            float*            offset,
            float             radius,
            size_t            axis_0,
            size_t            axis_1,
            size_t            axis_linear,
            bool              is_clockwise_arc) {
    float center_axis0 = position[axis_0] + offset[axis_0];
    float center_axis1 = position[axis_1] + offset[axis_1];
    float r_axis0      = -offset[axis_0];  // Radius vector from center to current location
    float r_axis1      = -offset[axis_1];
    float rt_axis0     = target[axis_0] - center_axis0;
    float rt_axis1     = target[axis_1] - center_axis1;

    float previous_position[MAX_N_AXIS] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

    uint16_t n;
    auto     n_axis = config->_axes->_numberAxis;
    for (n = 0; n < n_axis; n++) {
        previous_position[n] = position[n];
    }

    // CCW angle between position and target from circle center. Only one atan2() trig computation required.
    float angular_travel = float(atan2(r_axis0 * rt_axis1 - r_axis1 * rt_axis0, r_axis0 * rt_axis0 + r_axis1 * rt_axis1));
    if (is_clockwise_arc) {  // Correct atan2 output per direction
        if (angular_travel >= -ARC_ANGULAR_TRAVEL_EPSILON) {
            angular_travel -= 2 * float(M_PI);
        }
    } else {
        if (angular_travel <= ARC_ANGULAR_TRAVEL_EPSILON) {
            angular_travel += 2 * float(M_PI);
        }
    }

    auto mconfig = config;

    // NOTE: Segment end points are on the arc, which can lead to the arc diameter being smaller by up to
    // (2x) arc_tolerance. For 99% of users, this is just fine. If a different arc segment fit
    // is desired, i.e. least-squares, midpoint on arc, just change the mm_per_arc_segment calculation.
    // For most uses, this value should not exceed 2000.
    uint16_t segments =
        uint16_t(floor(fabs(0.5 * angular_travel * radius) / sqrt(mconfig->_arcTolerance * (2 * radius - mconfig->_arcTolerance))));
    if (segments) {
        // Multiply inverse feed_rate to compensate for the fact that this movement is approximated
        // by a number of discrete segments. The inverse feed_rate should be correct for the sum of
        // all segments.
        if (pl_data->motion.inverseTime) {
            pl_data->feed_rate *= segments;
            pl_data->motion.inverseTime = 0;  // Force as feed absolute mode over arc segments.
        }
        float theta_per_segment  = angular_travel / segments;
        float linear_per_segment = (target[axis_linear] - position[axis_linear]) / segments;
        /* Vector rotation by transformation matrix: r is the original vector, r_T is the rotated vector,
           and phi is the angle of rotation. Solution approach by Jens Geisler.
               r_T = [cos(phi) -sin(phi);
                      sin(phi)  cos(phi] * r ;

           For arc generation, the center of the circle is the axis of rotation and the radius vector is
           defined from the circle center to the initial position. Each line segment is formed by successive
           vector rotations. Single precision values can accumulate error greater than tool precision in rare
           cases. So, exact arc path correction is implemented. This approach avoids the problem of too many very
           expensive trig operations [sin(),cos(),tan()] which can take 100-200 usec each to compute.

           Small angle approximation may be used to reduce computation overhead further. A third-order approximation
           (second order sin() has too much error) holds for most, if not, all CNC applications. Note that this
           approximation will begin to accumulate a numerical drift error when theta_per_segment is greater than
           ~0.25 rad(14 deg) AND the approximation is successively used without correction several dozen times. This
           scenario is extremely unlikely, since segment lengths and theta_per_segment are automatically generated
           and scaled by the arc tolerance setting. Only a very large arc tolerance setting, unrealistic for CNC
           applications, would cause this numerical drift error. However, it is best to set N_ARC_CORRECTION from a
           low of ~4 to a high of ~20 or so to avoid trig operations while keeping arc generation accurate.

           This approximation also allows mc_arc to immediately insert a line segment into the planner
           without the initial overhead of computing cos() or sin(). By the time the arc needs to be applied
           a correction, the planner should have caught up to the lag caused by the initial mc_arc overhead.
           This is important when there are successive arc motions.
        */
        // Computes: cos_T = 1 - theta_per_segment^2/2, sin_T = theta_per_segment - theta_per_segment^3/6) in ~52usec
        float cos_T = 2.0f - theta_per_segment * theta_per_segment;
        float sin_T = theta_per_segment * 0.16666667f * (cos_T + 4.0f);
        cos_T *= 0.5;
        float    sin_Ti;
        float    cos_Ti;
        float    r_axisi;
        uint16_t i;
        size_t   count             = 0;
        float    original_feedrate = pl_data->feed_rate;  // Kinematics may alter the feedrate, so save an original copy
        for (i = 1; i < segments; i++) {                  // Increment (segments-1).
            if (count < N_ARC_CORRECTION) {
                // Apply vector rotation matrix. ~40 usec
                r_axisi = r_axis0 * sin_T + r_axis1 * cos_T;
                r_axis0 = r_axis0 * cos_T - r_axis1 * sin_T;
                r_axis1 = r_axisi;
                count++;
            } else {
                // Arc correction to radius vector. Computed only every N_ARC_CORRECTION increments. ~375 usec
                // Compute exact location by applying transformation matrix from initial radius vector(=-offset).
                cos_Ti  = float(cos(i * theta_per_segment));
                sin_Ti  = float(sin(i * theta_per_segment));
                r_axis0 = -offset[axis_0] * cos_Ti + offset[axis_1] * sin_Ti;
                r_axis1 = -offset[axis_0] * sin_Ti - offset[axis_1] * cos_Ti;
                count   = 0;
            }
            // Update arc_target location
            position[axis_0] = center_axis0 + r_axis0;
            position[axis_1] = center_axis1 + r_axis1;
            position[axis_linear] += linear_per_segment;
            pl_data->feed_rate = original_feedrate;  // This restores the feedrate kinematics may have altered
            mc_linear(position, pl_data, previous_position);
            previous_position[axis_0]      = position[axis_0];
            previous_position[axis_1]      = position[axis_1];
            previous_position[axis_linear] = position[axis_linear];
            // Bail mid-circle on system abort. Runtime command check already performed by mc_linear.
            if (sys.abort) {
                return;
            }
        }
    }
    // Ensure last segment arrives at target location.
    mc_linear(target, pl_data, previous_position);
}

// Execute dwell in seconds.
bool mc_dwell(int32_t milliseconds) {
    if (milliseconds <= 0 || sys.state == State::CheckMode) {
        return false;
    }
    protocol_buffer_synchronize();
    return delay_msec(milliseconds, DwellMode::Dwell);
}

// Perform homing cycle to locate and set machine zero. Only '$H' executes this command.
// NOTE: There should be no motions in the buffer and the system must be in idle state before
// executing the homing cycle. This prevents incorrect buffered plans after homing.
void mc_homing_cycle(AxisMask axis_mask) {
    if (config->_kinematics->kinematics_homing(axis_mask)) {
        // Allow kinematics to replace homing.
        // TODO: Better integrate this logic.
        return;
    }

    // Abort homing cycle if an axis has limit switches engaged on both ends,
    // or if it is impossible to tell which end is engaged.  In that situation
    // we do not know the pulloff direction.
    if (ambiguousLimit()) {
        mc_reset();  // Issue system reset and ensure spindle and coolant are shutdown.
        rtAlarm = ExecAlarm::HardLimit;
        return;
    }

    // Might set an alarm; if so protocol_execute_realtime will handle it
    Machine::Homing::run_cycles(axis_mask);

    protocol_execute_realtime();  // Check for reset and set system abort.
    if (sys.abort) {
        return;  // Did not complete. Alarm state set by mc_alarm.
    }
    // Homing cycle complete! Setup system for normal operation.
    // -------------------------------------------------------------------------------------
    // Sync gcode parser and planner positions to homed position.
    gc_sync_position();
    plan_sync_position();
    // This give kinematics a chance to do something after normal homing
    config->_kinematics->kinematics_post_homing();
}

volatile ProbeState probeState;

bool probe_succeeded = false;

// Perform tool length probe cycle. Requires probe switch.
// NOTE: Upon probe failure, the program will be stopped and placed into ALARM state.
GCUpdatePos mc_probe_cycle(float* target, plan_line_data_t* pl_data, uint8_t parser_flags) {
    if (!config->_probe->exists()) {
        log_error("Probe pin is not configured");
        return GCUpdatePos::None;
    }
    // TODO: Need to update this cycle so it obeys a non-auto cycle start.
    if (sys.state == State::CheckMode) {
        return config->_probe->_check_mode_start ? GCUpdatePos::None : GCUpdatePos::Target;
    }
    // Finish all queued commands and empty planner buffer before starting probe cycle.
    protocol_buffer_synchronize();
    if (sys.abort) {
        return GCUpdatePos::None;  // Return if system reset has been issued.
    }

    config->_stepping->beginLowLatency();

    // Initialize probing control variables
    bool is_probe_away = bits_are_true(parser_flags, GCParserProbeIsAway);
    bool is_no_error   = bits_are_true(parser_flags, GCParserProbeIsNoError);
    probe_succeeded    = false;  // Re-initialize probe history before beginning cycle.
    config->_probe->set_direction(is_probe_away);
    // After syncing, check if probe is already triggered. If so, halt and issue alarm.
    // NOTE: This probe initialization error applies to all probing cycles.
    if (config->_probe->tripped()) {
        rtAlarm = ExecAlarm::ProbeFailInitial;
        protocol_execute_realtime();
        config->_stepping->endLowLatency();
        return GCUpdatePos::None;  // Nothing else to do but bail.
    }
    // Setup and queue probing motion. Auto cycle-start should not start the cycle.
    log_info("Found");
    mc_linear(target, pl_data, gc_state.position);
    // Activate the probing state monitor in the stepper module.
    probeState = ProbeState::Active;
    // Perform probing cycle. Wait here until probe is triggered or motion completes.
    rtCycleStart = true;
    do {
        pollChannels();
        protocol_execute_realtime();
        if (sys.abort) {
            config->_stepping->endLowLatency();
            return GCUpdatePos::None;  // Check for system abort
        }
    } while (sys.state != State::Idle);

    config->_stepping->endLowLatency();

    // Probing cycle complete!
    // Set state variables and error out, if the probe failed and cycle with error is enabled.
    if (probeState == ProbeState::Active) {
        if (is_no_error) {
            memcpy(probe_steps, motor_steps, sizeof(motor_steps));
        } else {
            rtAlarm = ExecAlarm::ProbeFailContact;
        }
    } else {
        probe_succeeded = true;  // Indicate to system the probing cycle completed successfully.
    }
    probeState = ProbeState::Off;  // Ensure probe state monitor is disabled.
    protocol_execute_realtime();   // Check and execute run-time commands
    // Reset the stepper and planner buffers to remove the remainder of the probe motion.
    Stepper::reset();      // Reset step segment buffer.
    plan_reset();          // Reset planner buffer. Zero planner positions. Ensure probing motion is cleared.
    plan_sync_position();  // Sync planner position to current machine position.
    if (MESSAGE_PROBE_COORDINATES) {
        // All done! Output the probe position as message.
        report_probe_parameters(allChannels);
    }
    if (probe_succeeded) {
        return GCUpdatePos::System;  // Successful probe cycle.
    } else {
        return GCUpdatePos::Target;  // Failed to trigger probe within travel. With or without error.
    }
}

// Plans and executes the single special motion case for parking. Independent of main planner buffer.
// NOTE: Uses the always free planner ring buffer head to store motion parameters for execution.
void mc_parking_motion(float* parking_target, plan_line_data_t* pl_data) {
    if (sys.abort) {
        return;  // Block during abort.
    }
    if (plan_buffer_line(parking_target, pl_data)) {
        sys.step_control.executeSysMotion = true;
        sys.step_control.endMotion        = false;  // Allow parking motion to execute, if feed hold is active.
        Stepper::parking_setup_buffer();            // Setup step segment buffer for special parking motion case
        Stepper::prep_buffer();
        Stepper::wake_up();
        do {
            protocol_exec_rt_system();
            if (sys.abort) {
                return;
            }
        } while (sys.step_control.executeSysMotion);
        Stepper::parking_restore_buffer();  // Restore step segment buffer to normal run state.
    } else {
        sys.step_control.executeSysMotion = false;
        protocol_exec_rt_system();
    }
}

void mc_override_ctrl_update(Override override_state) {
    // Finish all queued commands before altering override control state
    protocol_buffer_synchronize();
    if (sys.abort) {
        return;
    }
    sys.override_ctrl = override_state;
}

// Method to ready the system to reset by setting the realtime reset command and killing any
// active processes in the system. This also checks if a system reset is issued while in
// motion state. If so, kills the steppers and sets the system alarm to flag position
// lost, since there was an abrupt uncontrolled deceleration. Called at an interrupt level by
// realtime abort command and hard limits. So, keep to a minimum.  Stuff that cannot be
// done quickly is handled later when Protocol.cpp responds to rtReset.
void mc_reset() {
    // Only this function can set the system reset. Helps prevent multiple kill calls.
    if (!rtReset) {
        rtReset = true;

        // Kill steppers only if in any motion state, i.e. cycle, actively holding, or homing.
        // NOTE: If steppers are kept enabled via the step idle delay setting, this also keeps
        // the steppers enabled by avoiding the go_idle call altogether, unless the motion state is
        // violated, by which, all bets are off.
        if ((sys.state == State::Cycle || sys.state == State::Homing || sys.state == State::Jog) ||
            (sys.step_control.executeHold || sys.step_control.executeSysMotion)) {
            if (sys.state == State::Homing) {
                if (rtAlarm == ExecAlarm::None) {
                    rtAlarm = ExecAlarm::HomingFailReset;
                }
            } else {
                rtAlarm = ExecAlarm::AbortCycle;
            }
            Stepper::go_idle();  // Stop stepping immediately, possibly losing position
        }
        config->_stepping->reset();
    }
}
