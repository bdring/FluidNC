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
        // XXX this will not work if there dual motors on the Z axis.
        bool stop = bits_are_true(axisMask, limited);
        clear_bits(axisMask, limited);
        return stop;
    }

    // plan a homing move in motor space for the homing sequence
    uint32_t CoreXY::homingMove(AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;

        uint32_t settle = 0;
        float    ratesq = 0;

        float cartesian_target[n_axis] = { 0 };

        for (int axis = X_AXIS; axis <= config->_axes->_numberAxis; axis++) {
            if (bitnum_is_false(axisMask, axis)) {
                continue;
            }

            set_motor_steps(axis, 0);

            auto axisConfig = axes->_axis[axis];
            auto homing     = axisConfig->_homing;

            settle = std::max(settle, homing->_settle_ms);

            float axis_rate;
            float travel;
            switch (phase) {
                case Machine::Homing::Phase::FastApproach:
                    axis_rate = homing->_seekRate;
                    travel    = axisConfig->_maxTravel * homing->_seek_scaler;
                    break;
                case Machine::Homing::Phase::SlowApproach:
                    axis_rate = homing->_feedRate;
                    travel    = axisConfig->_motors[0]->_pulloff * homing->_feed_scaler;
                    break;
                case Machine::Homing::Phase::PrePulloff:
                case Machine::Homing::Phase::Pulloff0:
                case Machine::Homing::Phase::Pulloff1:
                    axis_rate = homing->_feedRate;
                    travel    = -axisConfig->_motors[0]->_pulloff;
                    break;
                case Machine::Homing::Phase::Pulloff2:
                    log_error("Pulloff2 phase in CoreXY homing");
                    break;
            }
            // Set target direction based on various factors
            switch (phase) {
                case Machine::Homing::Phase::PrePulloff: {
#if 0
                    // For PrePulloff, the motion depends on which switches are active.
                    MotorMask axisMotors = Machine::Axes::axes_to_motors(1 << axis);
                    bool      posLimited = bits_are_true(Machine::Axes::posLimitMask, axisMotors);
                    bool      negLimited = bits_are_true(Machine::Axes::negLimitMask, axisMotors);
                    if (posLimited && negLimited) {
                        log_error("Both positive and negative limit switches are active for axis " << axes->axisName(axis));
                        // xxx need to abort somehow
                        return 0;
                    }
                    if (posLimited) {
                        target[axis] = -travel;
                    } else if (negLimited) {
                        target[axis] = travel;
                    } else {
                        target[axis] = 0;
                    }
#else
// XXX implement me
#endif
                } break;

                case Machine::Homing::Phase::FastApproach:
                case Machine::Homing::Phase::SlowApproach:
                    target[axis] = homing->_positiveDirection ? travel : -travel;
                    break;

                case Machine::Homing::Phase::Pulloff0:
                case Machine::Homing::Phase::Pulloff1:
                case Machine::Homing::Phase::Pulloff2:
                    target[axis] = homing->_positiveDirection ? -travel : travel;
                    break;
            }

            ratesq += (axis_rate * axis_rate);

            cartesian_target[axis] = travel;
        }

        rate = sqrtf(ratesq);
        //TODO Need to adjust the rate.  Maybe transform_cartesian_to_motors should do it

        transform_cartesian_to_motors(target, cartesian_target);

        log_debug("CoreXY axes " << cartesian_target[0] << "," << cartesian_target[1] << "," << cartesian_target[2] << " motors "
                                 << target[0] << "," << target[1] << "," << target[2]);

        return settle;
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
        //log_info("cartesian_to_motors position (" << position[X_AXIS] << "," << position[Y_AXIS] << ")");

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
    void CoreXY::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[X_AXIS] = (_x_scaler * cartesian[X_AXIS]) + cartesian[Y_AXIS];
        motors[Y_AXIS] = (_x_scaler * cartesian[X_AXIS]) - cartesian[Y_AXIS];

        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = Z_AXIS; axis < n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<CoreXY> registration("CoreXY");
    }
}
