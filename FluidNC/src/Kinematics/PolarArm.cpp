#include "PolarArm.h"

#include "../Machine/MachineConfig.h"

#include <cmath>
/*
Default configuration

kinematics:
  PolarArm:
    arm_length: 762
    straight_y_position: 228.6
    segment_length = 1.0
*/

namespace Kinematics {
    void PolarArm::group(Configuration::HandlerBase& handler) {
        handler.item("arm_length", _arm_length);
        handler.item("straight_y_position", _straight_y_position);
        handler.item("segment_length", _segment_length);
    }

    void PolarArm::init() {
        log_info("Kinematic system: " << name());
        arm_length_squared = _arm_length * _arm_length;
    }

    bool PolarArm::kinematics_homing(AxisMask cycle_mask) {
        auto  n_axis           = config->_axes->_numberAxis;
        float mpos[MAX_N_AXIS] = { 0 };
        float motors_mm[MAX_N_AXIS];
        transform_cartesian_to_motors(motors_mm, mpos);
        for (int axis = X_AXIS; axis < n_axis; axis++) {
            motor_steps[axis] = mpos_to_steps(motors_mm[axis], axis);
            log_info("axis pos: " << motors_mm[axis]);
        }
        sys.step_control = {};  // Return step control to normal operation.
        log_info("homed:");
        gc_sync_position();
        plan_sync_position();
        return true;
    }

    void PolarArm::kinematics_post_homing() {
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

    bool PolarArm::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        float    dx, dy, dz;  // distances in each cartesian axis
        float    seg_percent;
        uint32_t segment_count;
        float    dx_seg, dy_seg, dz_seg;
        //log_info("cartesian_to_motors position (" << position[X_AXIS] << "," << position[Y_AXIS] << ")");

        // calculate cartesian move distance for each axis
        dx = target[X_AXIS] - position[X_AXIS];
        dy = target[Y_AXIS] - position[Y_AXIS];
        dz = target[Z_AXIS] - position[Z_AXIS];

        auto n_axis = config->_axes->_numberAxis;

        float motors[MAX_N_AXIS];
        if (pl_data->motion.rapidMotion) {
            segment_count = 1;  // rapid G0 motion is not used to draw, so skip the segmentation
        } else {
            // determine the number of segments we need ... round up so there is at least 1
            // if position does not change in Y no need to segment
            segment_count = 1;
            if (dy != 0) {
                segment_count = ceil(abs(dy / _segment_length));
            }
        }
        dx_seg = dx / float(segment_count);
        dy_seg = dy / float(segment_count);
        dz_seg = dz / float(segment_count);
        for (uint32_t segment = 1; segment <= segment_count; segment++) {
            // determine this segment's absolute target
            float seg_positions[MAX_N_AXIS] = { position[X_AXIS] + (dx_seg * segment),
                                                position[Y_AXIS] + (dy_seg * segment),
                                                position[Z_AXIS] + (dz_seg * segment) };
            transform_cartesian_to_motors(motors, seg_positions);
            if (!mc_move_motors(motors, pl_data)) {
                return false;
            }
        }
        return true;
    }

    /*
    The status command uses motors_to_cartesian() to convert
    your motor positions to cartesian X,Y,Z... coordinates.

    Convert the MAX_N_AXIS array of motor positions to cartesian in your code.
    */
    void PolarArm::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        cartesian[Y_AXIS] = motors[Y_AXIS];
        float x_offset    = calculate_x_offset(motors[Y_AXIS] - _straight_y_position);
        cartesian[X_AXIS] = (motors[X_AXIS] - x_offset);
        // cartesian[X_AXIS] = motors[X_AXIS];
        for (int axis = Z_AXIS; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    /*
    Kinematic equations
    */
    void PolarArm::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[Y_AXIS] = cartesian[Y_AXIS];
        float x_offset = calculate_x_offset(cartesian[Y_AXIS] - _straight_y_position);
        motors[X_AXIS] = cartesian[X_AXIS] + x_offset;
        int n_axis     = config->_axes->_numberAxis;
        for (uint8_t axis = Z_AXIS; axis <= n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    float PolarArm::calculate_x_offset(float y_pos) { return _arm_length - sqrt((_arm_length * _arm_length) - (y_pos * y_pos)); }

    // Determine the unit distance between (2) 3D points
    // TODO. This might below in nut & bolts as a helper function for other uses.
    float PolarArm::three_axis_dist(float* point1, float* point2) {
        return sqrt(((point1[0] - point2[0]) * (point1[0] - point2[0])) + ((point1[1] - point2[1]) * (point1[1] - point2[1])) +
                    ((point1[2] - point2[2]) * (point1[2] - point2[2])));
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<PolarArm> registration("PolarArm");
    }
}
