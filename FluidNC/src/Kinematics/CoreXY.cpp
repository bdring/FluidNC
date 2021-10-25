#include "CoreXY.h"

#include "../Machine/MachineConfig.h"
#include "../Limits.h"  // limits_soft_check

#include "../Protocol.h"  // protocol_execute_realtime

#include <cmath>

/*
Default configuration

kinematics:
  CoreXY:
    x_scaler: 1
    y_scaler: 1

Scaling factors are made for midTbot type machines.

TODO: Implement scalers

On a midTbot the motors themselves move in X or Y so they need to be compensated. It 
would use x_scaler: 1 on bots where the motors move in X

TODO: Don't use Y yet! Leave at 1...need to test

*/

namespace Kinematics {
    void CoreXY::group(Configuration::HandlerBase& handler) {
        handler.item("x_scaler", _x_scaler);
        handler.item("y_scaler", _y_scaler);

        handler.item("segment_length", _segment_length);
    }

    void CoreXY::init() {
        log_info("Kinematic system: " << name());
        // only print scalers if not default values
        if (_x_scaler != 1.0 || _y_scaler != 1.0) {
            log_info(name() << " x scaler:" << _x_scaler << " y_scaler:" << _y_scaler);
        }
    }

    bool CoreXY::kinematics_homing(AxisMask cycle_mask) {
       
        if (ambiguousLimit()) {
            // TODO: Maybe ambiguousLimit() should do this stuff because this could be a several places
            mc_reset();  // Issue system reset and ensure spindle and coolant are shutdown.
            rtAlarm = ExecAlarm::HardLimit;

            return false;
        }

        // multi-axis cycles not allowed


        // run cycles
        

        // Do nothing.
        return false;
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
        //calc_fwd[X_AXIS] = 0.5 / geometry_factor * (position[X_AXIS] + position[Y_AXIS]);
        cartesian[X_AXIS] = 0.5 * (motors[X_AXIS] + motors[Y_AXIS]);
        cartesian[Y_AXIS] = 0.5 * (motors[X_AXIS] - motors[Y_AXIS]);

        for (int axis = Z_AXIS; axis < n_axis; axis++) {
            cartesian[axis] = motors[axis];
        }
    }

    /*
      limitsCheckTravel() is called to check soft limits
      It returns true if the motion is outside the limit values
    */
    bool CoreXY::limitsCheckTravel(float* target) { return false; }

    /*
    Kinematic equations
    */

    void CoreXY::transform_cartesian_to_motors(float* motors, float* cartesian) {
        motors[X_AXIS] = cartesian[X_AXIS] + cartesian[Y_AXIS];
        motors[Y_AXIS] = cartesian[X_AXIS] - cartesian[Y_AXIS];

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
