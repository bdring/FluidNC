#include "CoreXY.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // limits_soft_check
#include "../Machine/Homing.h"

#include "../Protocol.h"  // protocol_execute_realtime

#include <cmath>

/*
Default configuration

kinematics:
  CoreXY:
    x_scaler: 1

Scaling factors are made for midTbot type machines.

TODO: Implement scalers

On a midTbot the motors themselves move in X or Y so they need to be compensated. It 
would use x_scaler: 1 on bots where the motors move in X

TODO: If touching back off

*/

namespace Kinematics {
    void CoreXY::group(Configuration::HandlerBase& handler) {}

    void CoreXY::init() { log_info("Kinematic system: " << name()); }

    // plan a homing mve in motor space for the homing sequence
    void CoreXY::plan_homing_move(AxisMask axisMask, bool approach, bool seek) {
        float   rate   = 0;
        float*  target = get_mpos();
        float   dist   = 0;
        uint8_t axis   = X_AXIS;

        if (bitnum_is_true(axisMask, Y_AXIS)) {
            axis = Y_AXIS;
        }

        auto axisConf = config->_axes->_axis[axis];

        if (seek) {
            dist = axisConf->_maxTravel * axisConf->_homing->_seek_scaler;
            rate = axisConf->_homing->_seekRate;
        } else {
            dist = axisConf->_motors[0]->_pulloff;
            rate = axisConf->_homing->_feedRate;
            if (!approach) {
                dist *= -1.000;                           // backoff
            } else {                                      // approach
                dist *= axisConf->_homing->_feed_scaler;  // times scaler to make sure we hit
            }
        }

        if (!axisConf->_homing->_positiveDirection) {
            dist *= -1.000;
        }

        float move_to[MAX_N_AXIS] = { 0 };
        // zero all X&Y posiitons before each cycle
        // leave other axes unchanged
        for (int axis = X_AXIS; axis <= config->_axes->_numberAxis; axis++) {
            if (axis < Z_AXIS) {
                motor_steps[axis] = 0.0;
                target[axis]      = 0.0;
            } else {
                move_to[axis] = target[axis];
            }
        }

        //TODO Need to adjust the rate for CoreXY 1.414

        (axis == X_AXIS) ? move_to[X_AXIS] = dist : move_to[Y_AXIS] = dist;

        transform_cartesian_to_motors(target, move_to);

        plan_line_data_t plan_data;
        plan_data.spindle_speed         = 0;
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.spindle               = SpindleState::Disable;
        plan_data.coolant.Mist          = 0;
        plan_data.coolant.Flood         = 0;
        plan_data.line_number           = 0;
        plan_data.is_jog                = false;
        plan_data.feed_rate             = rate;  // Magnitude of homing rate vector

        plan_buffer_line(target, &plan_data);  // Bypass mc_move_motors(). Directly plan homing motion.

        sys.step_control                  = {};
        sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
        Stepper::prep_buffer();                    // Prep and fill segment buffer from newly planned block.
        Stepper::wake_up();                        // Initiate motion

        // The move has started. There are 2 good outcomes we are looking for
        // On approach we are looking for a switch touch
        // On backoff we are waiting for the motion to stop.
        // All other outcomes are problems

        bool switch_touch = false;
        do {
            if (approach) {
                switch_touch = bitnum_is_true((Machine::Axes::posLimitMask | Machine::Axes::negLimitMask), axis);
            }

            Stepper::prep_buffer();  // Check and prep segment buffer.

            // This checks some of the events that would normally be handled
            // by protocol_execute_realtime().  The homing loop is time-critical
            // so we handle those events directly here, calling protocol_execute_realtime()
            // only if one of those events is active.
            if (rtReset) {
                throw ExecAlarm::HomingFailReset;
            }
            if (rtSafetyDoor) {
                throw ExecAlarm::HomingFailDoor;
            }
            if (rtCycleStop) {
                //log_info("CoreXY Cyclestop");
                rtCycleStop = false;
                if (approach) {
                    throw ExecAlarm::HomingFailApproach;
                }
                if (bitnum_is_true((Machine::Axes::posLimitMask | Machine::Axes::negLimitMask), axis)) {
                    // Homing failure: Limit switch still engaged after pull-off motion
                    throw ExecAlarm::HomingFailPulloff;
                }

                switch_touch = true;  // used to break out of the do loop
            }
            pollChannels();
        } while (!switch_touch);

        Stepper::reset();  // Immediately force kill steppers and reset step segment buffer.
        delay_ms(axisConf->_homing->_settle_ms);
    }

    bool CoreXY::kinematics_homing(AxisMask cycle_mask) {  // TODO cycle_mask s/b axisMask...this is not about cycles
        // make sure there are no axes that are not in homingMask
        if (cycle_mask && !(cycle_mask & Machine::Axes::homingMask)) {
            log_error("Not a homed axis:");
            return true;
        }

        if (ambiguousLimit()) {
            // TODO: Maybe ambiguousLimit() should do this stuff because this could be a several places
            mc_reset();  // Issue system reset and ensure spindle and coolant are shutdown
            rtAlarm = ExecAlarm::HardLimit;
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return true;
        }

        if (cycle_mask != 0) {
            if (bitnum_is_true(cycle_mask, X_AXIS) || bitnum_is_true(cycle_mask, X_AXIS)) {
                log_error("CoreXY cannot single axis home X or Y axes");
                // TODO: Set some Kinematics error or alarm
                return true;
            }
            Machine::Homing::run_one_cycle(cycle_mask);
            return true;
        }

        // Multi-axis cycles not allowed with CoreXY because 2 motors are used for linear XY moves
        // Check each cycle for multiple axes
        for (int cycle = 1; cycle <= MAX_N_AXIS; cycle++) {
            AxisMask axisMask = Machine::Homing::axis_mask_from_cycle(cycle);
            uint8_t  count    = 0;

            for (int i = 0; i < 16; i++) {
                if (bitnum_is_true(axisMask, i)) {
                    if (++count > 1) {  // Error with any axis with more than one axis per cycle
                        log_error("CoreXY cannot multi-axis home. Check homing cycle:" << cycle);
                        // TODO: Set some Kinematics error or alarm
                        return true;
                    }
                }
            }
        }

        // run cycles
        for (int cycle = 1; cycle <= MAX_N_AXIS; cycle++) {
            AxisMask axisMask = Machine::Homing::axis_mask_from_cycle(cycle);

            if (!axisMask)
                continue;

            // Only X and Y need a special homing sequence
            if (!bitnum_is_true(axisMask, X_AXIS) && !bitnum_is_true(axisMask, Y_AXIS)) {
                Machine::Homing::run_one_cycle(axisMask);
                continue;
            } else {
                //log_info("CoreXY homing cycle:" << cycle << " axis:" << axisMask);
                if (bitnum_is_true(axisMask, X_AXIS) || bitnum_is_true(axisMask, Y_AXIS)) {
                    try {
                        plan_homing_move(axisMask, true, true);    // seek aproach
                        plan_homing_move(axisMask, false, false);  // pulloff
                        plan_homing_move(axisMask, true, false);   // feed aproach
                        plan_homing_move(axisMask, false, false);  // pulloff
                    } catch (ExecAlarm alarm) {
                        rtAlarm = alarm;
                        config->_axes->set_homing_mode(axisMask, false);  // tell motors homing is done...failed
                        log_error("Homing fail");
                        mc_reset();                   // Stop motors, if they are running.
                        protocol_execute_realtime();  // handle any pending rtXXX conditions
                        return true;
                    }
                }
            }
        }

        auto n_axis = config->_axes->_numberAxis;

        float mpos[MAX_N_AXIS] = { 0 };

        // Set machine positions for homed limit switches. Don't update non-homed axes.
        for (int axis = 0; axis < n_axis; axis++) {
            Machine::Axis* axisConf = config->_axes->_axis[axis];

            if (axisConf->_homing) {
                auto mpos_mm = axisConf->_homing->_mpos;
                auto pulloff = axisConf->_motors[0]->_pulloff;

                pulloff    = axisConf->_homing->_positiveDirection ? -pulloff : pulloff;
                mpos[axis] = mpos_mm + pulloff;
            }
        }

        float motors_mm[MAX_N_AXIS];
        transform_cartesian_to_motors(motors_mm, mpos);

        // the only single axis homing allowed is Z and above
        if (cycle_mask >= 1 << Z_AXIS) {
            for (int axis = Z_AXIS; axis < n_axis; axis++) {
                if (bitnum_is_true(cycle_mask, axis)) {
                    // set the Z motor position
                    motor_steps[axis] = mpos_to_steps(motors_mm[axis], axis);
                }
            }
        } else {
            // set all of them
            for (int axis = X_AXIS; axis < n_axis; axis++) {
                motor_steps[axis] = mpos_to_steps(motors_mm[axis], axis);
            }
        }

        sys.step_control = {};  // Return step control to normal operation.

        gc_sync_position();
        plan_sync_position();

        return true;
    }

    void CoreXY::kinematics_post_homing() {
        // Do nothing.
    }

    /*
      cartesian_to_motors() converts from cartesian coordinates to motor space.

      All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

      Parameters:
        target = an MAX_N_AXIS array of target positions (where the move is supposed to go)
        pl_data = planner data (see the definition of this type to see what it is)
        position = an MAX_N_AXIS array of where the machine is starting from for this move
    */
    bool CoreXY::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        float dx, dy, dz;  // distances in each cartesian axis

        //log_info("cartesian_to_motors position (" << position[X_AXIS] << "," << position[Y_AXIS] << ")");

        // calculate cartesian move distance for each axis
        dx         = target[X_AXIS] - position[X_AXIS];
        dy         = target[Y_AXIS] - position[Y_AXIS];
        dz         = target[Z_AXIS] - position[Z_AXIS];
        float dist = sqrt((dx * dx) + (dy * dy) + (dz * dz));

        auto n_axis = config->_axes->_numberAxis;

        float motors[MAX_N_AXIS];
        transform_cartesian_to_motors(motors, target);

        if (!pl_data->motion.rapidMotion) {
            float last_motors[MAX_N_AXIS];
            transform_cartesian_to_motors(last_motors, position);
            pl_data->feed_rate *= (three_axis_dist(motors, last_motors) / dist);
        }

        return mc_move_motors(motors, pl_data);

        // TO DO don't need a feedrate for rapids
        return true;
    }

    /*
      The status command uses motors_to_cartesian() to convert
      your motor positions to cartesian X,Y,Z... coordinates.

      Convert the MAX_N_AXIS array of motor positions to cartesian in your code.
    */
    void CoreXY::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // apply the forward kinemetics to the machine coordinates
        // https://corexy.com/theory.html
        cartesian[X_AXIS] = 0.5 * (motors[X_AXIS] + motors[Y_AXIS]) / _x_scaler;
        cartesian[Y_AXIS] = 0.5 * (motors[X_AXIS] - motors[Y_AXIS]);

        for (int axis = Z_AXIS; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    /*
    Kinematic equations
    */
    void CoreXY::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[X_AXIS] = (_x_scaler * cartesian[X_AXIS]) + cartesian[Y_AXIS];
        motors[Y_AXIS] = (_x_scaler * cartesian[X_AXIS]) - cartesian[Y_AXIS];

        auto n_axis = config->_axes->_numberAxis;
        for (uint8_t axis = Z_AXIS; axis <= n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    // Determine the unit distance between (2) 3D points
    // TODO. This might below in nut & bolts as a helper function for other uses.
    float CoreXY::three_axis_dist(float* point1, float* point2) {
        return sqrt(((point1[0] - point2[0]) * (point1[0] - point2[0])) + ((point1[1] - point2[1]) * (point1[1] - point2[1])) +
                    ((point1[2] - point2[2]) * (point1[2] - point2[2])));
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<CoreXY> registration("CoreXY");
    }
}
