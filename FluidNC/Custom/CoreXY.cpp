// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  CoreXY.cpp - 
  
  https://corexy.com/theory.html

  Limitations 
  - Must home via $H. $HX type homes not allowed
  - Must home one axis per cycle
  - limited to 3 axis systems...easy fix in increase (just donate)

    FYI: http://forums.trossenrobotics.com/tutorials/introduction-129/delta-robot-kinematics-3276/
    Better: http://hypertriangle.com/~alex/delta-robot-tutorial/
*/

#include "../src/Settings.h"
#include "../src/MotionControl.h"  // kinematics*
#include "../src/Limits.h"         // limitsCheckTravel

// Homing axis search distance multiplier. Computed by this value times the cycle travel.
#ifndef HOMING_AXIS_SEARCH_SCALAR
#    define HOMING_AXIS_SEARCH_SCALAR 1.1  // Must be > 1 to ensure limit switch will be engaged.
#endif
#ifndef HOMING_AXIS_LOCATE_SCALAR
#    define HOMING_AXIS_LOCATE_SCALAR 2.0  // Must be > 1 to ensure limit switch is cleared.
#endif

// The midTbot has a quirk where the x motor has to move twice as far as it would
// on a normal T-Bot or CoreXY
#ifndef MIDTBOT
const float geometry_factor = 1.0;
#else
const float geometry_factor = 2.0;
#endif

static float last_cartesian[MAX_N_AXIS] = {};

// prototypes for helper functions
float three_axis_dist(float* point1, float* point2);

void machine_init() {
    // print a startup message to show the kinematics are enable

#ifdef MIDTBOT
    log_info("CoreXY (midTbot) Kinematics Init");
#else
    log_info("CoreXY Kinematics Init");
#endif
}

// Converts Cartesian to motors with no motion control
static void cartesian_to_motors(float* position) {
    float motors[MAX_N_AXIS];

    motors[X_AXIS] = geometry_factor * position[X_AXIS] + position[Y_AXIS];
    motors[Y_AXIS] = geometry_factor * position[X_AXIS] - position[Y_AXIS];

    position[X_AXIS] = motors[X_AXIS];
    position[Y_AXIS] = motors[Y_AXIS];

    // Z and higher just pass through unchanged
}

// Cycle mask is 0 unless the user sends a single axis command like $HZ
// This will always return true to prevent the normal homing cycle
bool user_defined_homing(AxisMask cycle_mask) {
    if (!config->_homing) {
        log_error("Homing is not configured");
        return true;
    }
    uint8_t n_cycle;                       // each home is a multi cycle operation approach, pulloff, approach.....
    float   target[MAX_N_AXIS] = { 0.0 };  // The target for each move in the cycle
    float   max_travel;
    uint8_t axis;

    // check for multi axis homing per cycle ($Homing/Cycle0=XY type)...not allowed in CoreXY
    bool setting_error = false;
    auto n_axis        = config->_axes->_numberAxis;
    ;
    for (int cycle = 0; cycle < n_axis; cycle++) {
        if (numberOfSetBits(homing_cycle[cycle]->get()) > 1) {
            log_info("CoreXY Multi axis homing cycles not allowed. $Homing/Cycle" << cycle << "=" << homing_cycle[cycle]->getStringValue());
            setting_error = true;
        }
    }
    if (setting_error)
        return true;

    // setup the motion parameters
    plan_line_data_t  plan_data;
    plan_line_data_t* pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t));
    pl_data->motion                = {};
    pl_data->motion.systemMotion   = 1;
    pl_data->motion.noFeedOverride = 1;

    uint8_t cycle_count = (cycle_mask == 0) ? n_axis : 1;  // if we have a cycle_mask, we are only going to do one axis

    AxisMask mask = 0;
    for (int cycle = 0; cycle < cycle_count; cycle++) {
        // if we have a cycle_mask, do that. Otherwise get the cycle from the settings
        mask = cycle_mask ? cycle_mask : homing_cycle[cycle]->get();

        // If not X or Y do a normal home
        if (!(bitnum_is_true(mask, X_AXIS) || bitnum_is_true(mask, Y_AXIS))) {
            limits_go_home(mask);  // Homing cycle 0
            continue;              // continue to next item in for loop
        }

        mask = motors_set_homing_mode(mask, true);  // non standard homing motors will do their own thing and get removed from the mask

        for (uint8_t axis = X_AXIS; axis <= n_axis; axis++) {
            if (bitnum_to_mask(axis) == mask) {
                // setup for the homing of this axis
                bool  approach       = true;
                float homing_rate    = config->_homing->_seekRate;
                max_travel           = HOMING_AXIS_SEARCH_SCALAR * config->_axes->_axis[axis]->_maxTravel;
                sys.homing_axis_lock = 0xFF;                          // we don't need to lock any motors in CoreXY
                n_cycle              = (2 * NHomingLocateCycle + 1);  // approach + ((pulloff + approach) * Cycles)

                do {
                    bool switch_touched = false;

                    // zero all X&Y posiitons before each cycle
                    for (int idx = X_AXIS; idx <= Y_AXIS; idx++) {
                        motor_steps[idx] = 0.0;
                        target[idx]      = 0.0;
                    }

                    if (bits_are_true(homing_dir_mask->get(), bitnum_to_mask(axis))) {
                        approach ? target[axis] = -max_travel : target[axis] = max_travel;
                    } else {
                        approach ? target[axis] = max_travel : target[axis] = -max_travel;
                    }

                    for (int axis = Z_AXIS; axis < n_axis; axis++) {
                        target[axis] = steps_to_mpos(motor_steps[axis], axis);
                    }

                    // convert back to motor steps
                    cartesian_to_motors(target);

                    pl_data->feed_rate = homing_rate;   // feed or seek rates
                    plan_buffer_line(target, pl_data);  // Bypass mc_line(). Directly plan homing motion.
                    sys.step_control                  = {};
                    sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
                    st_prep_buffer();                          // Prep and fill segment buffer from newly planned block.
                    st_wake_up();                              // Initiate motion

                    do {
                        if (approach) {
                            switch_touched = limits_check(bitnum_to_mask(axis));
                        }
                        st_prep_buffer();  // Check and prep segment buffer. NOTE: Should take no longer than 200us.
                        // Exit routines: No time to run protocol_execute_realtime() in this loop.
                        if (rtSafetyDoor || rtReset || cycle_stop) {
                            ExecState rt_exec_state;
                            rt_exec_state.value = sys_rt_exec_state.value;
                            // Homing failure condition: Reset issued during cycle.
                            if (rtReset) {
                                rtAlarm = ExecAlarm::HomingFailReset;
                            }
                            // Homing failure condition: Safety door was opened.
                            if (rtSafetyDoor) {
                                rtAlarm = ExecAlarm::HomingFailDoor;
                            }
                            // Homing failure condition: Limit switch still engaged after pull-off motion
                            if (!approach && limits_check(cycle_mask)) {
                                rtAlarm = ExecAlarm::HomingFailPulloff;
                            }
                            // Homing failure condition: Limit switch not found during approach.
                            if (approach && cycle_stop) {
                                rtAlarm = ExecAlarm::HomingFailApproach;
                            }

                            if (rtAlarm != ExecAlarm::None) {
                                motors_set_homing_mode(cycle_mask, false);  // tell motors homing is done...failed
                                mc_reset();                                 // Stop motors, if they are running.
                                protocol_execute_realtime();
                                return true;
                            } else {
                                // Pull-off motion complete. Disable CYCLE_STOP from executing.
                                cycle_stop = false;
                                break;
                            }
                        }
                    } while (!switch_touched);

                    if (!approach) {
                        config->_stepping->synchronize();
                    }

                    st_reset();                            // Immediately force kill steppers and reset step segment buffer.
                    delay_ms(config->_homing->_debounce);  // Delay to allow transient dynamics to dissipate.

                    approach = !approach;
                    // After first cycle, homing enters locating phase. Shorten search to pull-off distance.
                    if (approach) {
                        max_travel  = config->_homing->_pulloff * HOMING_AXIS_LOCATE_SCALAR;
                        homing_rate = config->_homing->_feedRate;
                    } else {
                        max_travel  = config->_homing->_pulloff;
                        homing_rate = config->_homing->_seekRate;
                    }
                } while (n_cycle-- > 0);
            }
        }
    }  // for

    // after sussefully setting X & Y axes, we set the current positions

    // set the cartesian axis position
    for (axis = X_AXIS; axis <= Y_AXIS; axis++) {
        if (bitnum_is_true(homing_dir_mask->get(), axis)) {
            target[axis] = limitsMinPosition(axis) + config->_homing->_pulloff;
        } else {
            target[axis] = limitsMaxPosition(axis) - config->_homing->_pulloff;
        }
    }

    last_cartesian[X_AXIS] = target[X_AXIS];
    last_cartesian[Y_AXIS] = target[Y_AXIS];

    for (int axis = Z_AXIS; axis < n_axis; axis++) {
        last_cartesian[axis] = motor_steps[axis] / config->_axes->_axis[axis]->_stepsPerMm;
    }

    // convert to motors
    cartesian_to_motors(target);
    // convert to steps
    for (axis = X_AXIS; axis <= Y_AXIS; axis++) {
        motor_steps[axis] = target[axis] * config->_axes->_axis[axis]->_stepsPerMm;
    }

    sys.step_control = {};  // Return step control to normal operation.

    gc_sync_position();
    plan_sync_position();
    kinematics_post_homing();
    limits_init();

    return true;
}

static void transform_cartesian_to_motors(float* motors, float* cartesian) {
    motors[X_AXIS] = geometry_factor * cartesian[X_AXIS] + cartesian[Y_AXIS];
    motors[Y_AXIS] = geometry_factor * cartesian[X_AXIS] - cartesian[Y_AXIS];

    auto n_axis = config->_axes->_numberAxis;
    for (uint8_t axis = Z_AXIS; axis <= n_axis; axis++) {
        motors[axis] = cartesian[axis];
    }
}

// Inverse Kinematics calculates motor positions from real world cartesian positions
// position is the old machine position, target the new machine position
// Breaking into segments is not needed with CoreXY, because it is a linear system.
bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
    float dx, dy, dz;  // distances in each cartesian axis

    // calculate cartesian move distance for each axis
    dx         = target[X_AXIS] - position[X_AXIS];
    dy         = target[Y_AXIS] - position[Y_AXIS];
    dz         = target[Z_AXIS] - position[Z_AXIS];
    float dist = sqrt((dx * dx) + (dy * dy) + (dz * dz));

    auto n_axis = config->_axes->_numberAxis;

    float motors[n_axis];
    transform_cartesian_to_motors(motors, target);

    if (!pl_data->motion.rapidMotion) {
        float last_motors[n_axis];
        transform_cartesian_to_motors(last_motors, position);
        pl_data->feed_rate *= (three_axis_dist(motors, last_motors) / dist);
    }

    return mc_line(motors, pl_data);
}

// motors -> cartesian
void motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
    // apply the forward kinemetics to the machine coordinates
    // https://corexy.com/theory.html
    //calc_fwd[X_AXIS] = 0.5 / geometry_factor * (position[X_AXIS] + position[Y_AXIS]);
    cartesian[X_AXIS] = 0.5 * (motors[X_AXIS] + motors[Y_AXIS]) / geometry_factor;
    cartesian[Y_AXIS] = 0.5 * (motors[X_AXIS] - motors[Y_AXIS]);

    for (int axis = Z_AXIS; axis < n_axis; axis++) {
        cartesian[axis] = motors[axis];
    }
}

bool kinematics_pre_homing(AxisMask cycle_mask) {
    return false;
}

void kinematics_post_homing() {
    auto n_axis = config->_axes->_numberAxis;
    memcpy(gc_state.position, last_cartesian, n_axis * sizeof(last_cartesian[0]));
}

// this is used used by Limits.cpp to see if the range of the machine is exceeded.
bool limitsCheckTravel(float* target) {
    return false;
}

void user_m30() {}

// ================ Local Helper functions =================

// Determine the unit distance between (2) 3D points
float three_axis_dist(float* point1, float* point2) {
    return sqrt(((point1[0] - point2[0]) * (point1[0] - point2[0])) + ((point1[1] - point2[1]) * (point1[1] - point2[1])) +
                ((point1[2] - point2[2]) * (point1[2] - point2[2])));
}
