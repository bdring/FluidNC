#include "PolarArm.h"

#include "../Machine/MachineConfig.h"

#include <cmath>

/*
Default configuration

kinematics:
  WallPlotter:
    arm_length: 762
    straight_y_position: 228.6
*/

namespace Kinematics {
    void PolarArm::group(Configuration::HandlerBase& handler) {
        handler.item("arm_length", _arm_length);
        handler.item("straight_y_position", _straight_y_position);
    }

    void PolarArm::init() {
        log_info("Kinematic system: " << name());

        // We assume the machine starts at cartesian (0, 0, 0).
        // The motors assume they start from (0, 0, 0).
        // So we need to derive the zero lengths to satisfy the kinematic equations.
        //
        // TODO: Maybe we can change where the motors start, which would be simpler?
        arm_length_squared  = _arm_length * _arm_length;
    }

    bool PolarArm::kinematics_homing(AxisMask cycle_mask) {
        // Do nothing.
        return false;
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
    void PolarArm::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        cartesian[Y_AXIS] = motors[Y_AXIS] + _straight_y_position;
        float x_offset = calculate_x_offset(cartesian[Y_AXIS]);
        cartesian[X_AXIS] = (motors[X_AXIS] + x_offset);

        for (int axis = Z_AXIS; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    /*
    Kinematic equations
    */
    void PolarArm::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[Y_AXIS] = cartesian[Y_AXIS] - _straight_y_position;
        float x_offset = calculate_x_offset(cartesian[Y_AXIS]);
        motors[X_AXIS] =  cartesian[X_AXIS] - x_offset;
        int n_axis = config->_axes->_numberAxis;
        for (uint8_t axis = Y_AXIS; axis <= n_axis; axis++) {
            motors[axis] = cartesian[axis];
        }
    }

    float PolarArm::calculate_x_offset(float y_pos) {
        return _arm_length - sqrt(arm_length_squared - y_pos * y_pos);
    }
    /*
      limitsCheckTravel() is called to check soft limits
      It returns true if the motion is outside the limit values
    */
    bool PolarArm::limitsCheckTravel(float* target) {
        return false;
    }

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
