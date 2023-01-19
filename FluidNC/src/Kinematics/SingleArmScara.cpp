#include "SingleArmScara.h"

#include "../Machine/MachineConfig.h"

#include <cmath>

namespace Kinematics {
    void SingleArmScara::group(Configuration::HandlerBase& handler) {
        handler.item("upper_arm_mm", _upper_arm_mm);
        handler.item("forearm_mm", _forearm_mm);
        handler.item("segment_mm", _segment_length);
        handler.item("elbow_motor", _elbow_motor);
    }

    void SingleArmScara::init() {
        log_info("Kinematic system: " << name());
        init_position();
    }

    bool SingleArmScara::kinematics_homing(AxisMask axisMask) {
        auto             n_axis            = config->_axes->_numberAxis;
        float            cartesian[n_axis] = { 0.0 };
        float            motors[n_axis];
        plan_line_data_t pl_data;

        cartesian[0]      = _forearm_mm - _upper_arm_mm;
        cartesian[1]      = 0.1;  // just a tiny bit above to prevent crazyness.
        pl_data.feed_rate = config->_axes->_axis[X_AXIS]->_homing->_feedRate;

        xy_to_angles(cartesian, motors);

        mc_move_motors(motors, &pl_data);

        // disable the motors
        // manually
        log_info("SCARA here...I got this homing thing");

        return true;
    }

    // Initialize the machine position
    void SingleArmScara::init_position() {
        // initialize to the arms fully extended
        float cartesian[MAX_N_AXIS] = { 0.0 };
        cartesian[X_AXIS]           = _upper_arm_mm + _forearm_mm;

        set_motor_steps_from_mpos(cartesian);
    }

    void SingleArmScara::transform_cartesian_to_motors(float* motors, float* cartesian) {
        xy_to_angles(cartesian, motors);
    }

    /*
      cartesian_to_motors() converts from cartesian coordinates to motor space.

      All linear motions pass through cartesian_to_motors() to be planned as mc_move_motors operations.

      Parameters:
        target = an n_axis array of target positions (where the move is supposed to go)
        pl_data = planner data (see the definition of this type to see what it is)
        position = an n_axis array of where the machine is starting from for this move
    */
    bool SingleArmScara::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        //log_info("Go to cartesian x:" << target[X_AXIS] << " y:" << target[Y_AXIS]);

        float             dx, dy, dz;                 // segment distances in each cartesian axis
        uint32_t          segment_count;              // number of segments the move will be broken in to.
        plan_line_data_t* segment_pl_data = pl_data;  // copy the plan data. The feedrate will be changing

        auto n_axis = config->_axes->_numberAxis;

        float motor_segment_end[n_axis];

        float shoulder_motor_angle, elbow_motor_angle;
        float angles[2];
        float motors[n_axis];
        xy_to_angles(target, motors);

        return mc_move_motors(motors, pl_data);

        // ---------------------------------------- Temporary endpoint -------------------------

        float total_cartesian_distance = vector_distance(position, target, n_axis);

        // if move is zero do nothing and pass back the existing values
        if (total_cartesian_distance == 0) {
            xy_to_angles(target, motors);
            mc_move_motors(motors, pl_data);
            return true;
        }

        //float cartesian_feed_rate = pl_data->feed_rate;

        // calculate the total X,Y axis move distance
        // higher axes are the same in both coord systems, so it does not undergo conversion
        float xydist = vector_distance(target, position, 2);
        // Segment the moves. If we choose a small enough _segment_length we can hide the nonlinearity
        segment_count = xydist / _segment_length;
        if (segment_count < 1) {  // Make sure there is at least one segment, even if there is no movement
            segment_count = 1;
        }
        log_info("Segment Count:" << segment_count);

        float cartesian_segment_length = total_cartesian_distance / segment_count;

        // Calc length of each cartesian segment - The are the same for all segments.
        float cartesian_segment_components[n_axis];
        for (size_t axis = X_AXIS; axis < n_axis; axis++) {
            cartesian_segment_components[axis] = (target[axis] - position[axis]) / segment_count;
        }

        float cartesian_segment_end[n_axis];
        copyAxes(cartesian_segment_end, position);

        // Calculate desired cartesian feedrate distance ratio. Same for each seg.
        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            // calculate the cartesian end point of the next segment
            for (size_t axis = X_AXIS; axis < n_axis; axis++) {
                cartesian_segment_end[axis] += cartesian_segment_components[axis];
            }

            // Convert cartesian space coords to motor space
            float motor_segment_end[n_axis];
            xy_to_angles(cartesian_segment_end, motor_segment_end);

            // Remember the last motor position so the length can be computed the next time
            copyAxes(last_motor_segment_end, motor_segment_end);

            float motor_dist = vector_distance(last_motor_segment_end, motor_segment_end, 2);

            segment_pl_data->feed_rate = pl_data->feed_rate * (motor_dist / cartesian_segment_length);

            if (!mc_move_motors(motor_segment_end, segment_pl_data)) {
                // TODO fixup last_left last_right?? What is position state when jog is cancelled?
                return false;
            }
        }

        return true;
    }

    /*
      The status command uses motors_to_cartesian() to convert
      your motor positions to cartesian X,Y,Z... coordinates.

      Convert the n_axis array of motor positions to cartesian in your code.
    */
    void SingleArmScara::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        //log_info("motors_to_cartesian (" << motors[X_AXIS] << "," << motors[Y_AXIS] << ")");

        float L1 = _upper_arm_mm;
        float L2 = _forearm_mm;
        float D;
        float A1 = motors[0];
        float A2 = motors[1];
        float A3;

        if (!_elbow_motor) {
            A2 = A2 - A1;
        }

        D = sqrtf(L1 * L1 + L2 * L2 - (2 * L1 * L2 * cosf(A2)));

        float A4 = acosf((L1 * L1 + D * D - L2 * L2) / (2 * L1 * D));

        // if (A1 >= 0)
        //     A3 = (A1 - A4) * -1;
        // else
        A3 = A1 - A4;

        //log_info("D:" << D << " A3:" << A3 << " A4:" << A4);

        cartesian[X_AXIS] = cosf(A3) * D;
        cartesian[Y_AXIS] = sinf(A3) * D;
        // The rest of cartesian are not changed

        //log_info("cartesian (" << cartesian[X_AXIS] << "," << cartesian[Y_AXIS] << ")");
    }

    /*
    Kinematic equations
    */
    bool SingleArmScara::xy_to_angles(float* cartesian, float* angles) {
        //log_info("xy_to_angles xy:(" << cartesian[0] << "," << cartesian[1] << ")");

        float D = sqrtf(cartesian[0] * cartesian[0] + cartesian[1] * cartesian[1]);

        //float D = hypot_f(cartesian[0], cartesian[1]);

        if (D > (_upper_arm_mm + _forearm_mm)) {
            log_error("Location exceeds arm reach");
            return false;
        }

        // if (D < 20.0) {
        //     log_error("Tip and shoulder too close:" << D << " (" << cartesian[0] << "," << cartesian[1] << ")");
        //     return false;
        // }

        float L1 = _upper_arm_mm;
        float L2 = _forearm_mm;
        float A3 = atan2f(cartesian[1], cartesian[0]);
        float A4 = acosf((L1 * L1 + D * D - L2 * L2) / (2 * L1 * D));
        //log_info("A3:" << A3 << " A4:" << A4);

        angles[0] = A4 + A3;
        angles[1] = acosf((L1 * L1 + L2 * L2 - D * D) / (2 * L1 * L2));

        // if the motor is at the base we have to compensate for the motion of motor1
        //og_info("Go to angles (" << angles[0] << "," << angles[1] << ")");
        if (!_elbow_motor) {
            angles[1] = angles[1] + angles[0];
        }

        //log_info("L1:" << L1 << " L2:" << L2 << " D:" << D);
        //log_info("Go to angles (" << angles[0] << "," << angles[1] << ")");

        // copy motors not affected by kinematics
        for (size_t axis = Z_AXIS; axis < config->_axes->_numberAxis; axis++) {
            angles[axis] = cartesian[axis];
        }

        return true;
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<SingleArmScara> registration("SingleArmScara");
    }
}
