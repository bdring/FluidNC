#include "CoreXY.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // ambiguousLimit()
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

    void CoreXY::init() {
        log_info("Kinematic system: " << name());

        // A limit switch on either axis stops both motors
        config->_axes->_axis[X_AXIS]->_motors[0]->limitOtherAxis(Y_AXIS);
        config->_axes->_axis[Y_AXIS]->_motors[0]->limitOtherAxis(X_AXIS);
    }

    bool CoreXY::canHome(AxisMask axisMask) {
        // make sure there are no axes that are not in homingMask
        if (axisMask && !(axisMask & Machine::Axes::homingMask)) {
            log_error("Not a homed axis:");
            return false;
        }

        if (ambiguousLimit()) {
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return false;
        }
        return true;
    }

    bool CoreXY::limitReached(AxisMask& axisMask, MotorMask& motors, MotorMask limited) {
        // For CoreXY, the limit switches are associated with axes, not motors (multiple motors
        // must run to perform a move parallel to an axis).  When a limit switch is hit, we
        // clear the associated axis bit and stop motion.  The homing code will then replan
        // a new move along the remaining axes.
        MotorMask toClear = axisMask & limited;

        clear_bits(axisMask, limited);
        clear_bits(motors, limited);

        // During CoreXY homing of, say, the X axis, if the Y axis limit trips
        // it will stop both motors, motion will cease. and the X homing will
        // not finish.  To combat the, we release all the motors that are still
        // needed to finish the cycle.
        releaseMotors(axisMask, motors);

        return bool(toClear);
    }

    void CoreXY::releaseMotors(AxisMask axisMask, MotorMask motors) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (size_t axis = X_AXIS; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                axes->_axis[axis]->_motors[0]->unlimit();
            }
        }
    }

    /*
      cartesian_to_motors() converts from cartesian coordinates to motor space.

      All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

      Parameters:
        target = an n_axis array of target positions (where the move is supposed to go)
        pl_data = planner data (see the definition of this type to see what it is)
        position = an n_axis array of where the machine is starting from for this move
    */
    bool CoreXY::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        //        log_debug("cartesian_to_motors position (" << position[X_AXIS] << "," << position[Y_AXIS] << ") target (" << target[X_AXIS] << "," << target[Y_AXIS] << ")");

        auto n_axis = config->_axes->_numberAxis;

        float motors[n_axis];
        transform_cartesian_to_motors(motors, target);

        if (!pl_data->motion.rapidMotion) {
            // Calculate vector distance of the motion in cartesian coordinates
            float cartesian_distance = vector_distance(target, position, n_axis);

            // Calculate vector distance of the motion in motor coordinates
            float last_motors[n_axis];
            transform_cartesian_to_motors(last_motors, position);
            float motor_distance = vector_distance(motors, last_motors, n_axis);

            // Scale the feed rate by the motor/cartesian ratio
            pl_data->feed_rate *= motor_distance / cartesian_distance;
        }

        return mc_move_motors(motors, pl_data);
    }

    /*
      The status command uses motors_to_cartesian() to convert
      motor positions to cartesian X,Y,Z... coordinates.
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
    bool CoreXY::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[X_AXIS] = (_x_scaler * cartesian[X_AXIS]) + cartesian[Y_AXIS];
        motors[Y_AXIS] = (_x_scaler * cartesian[X_AXIS]) - cartesian[Y_AXIS];

        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
        return true;
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<CoreXY> registration("CoreXY");
    }
}
