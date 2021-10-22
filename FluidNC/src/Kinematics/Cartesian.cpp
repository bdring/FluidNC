#include "Cartesian.h"

#include "../Limits.h"
#include "../Machine/MachineConfig.h"

namespace Kinematics {
    void Cartesian::init() {
        config_message();
    }

    bool Cartesian::kinematics_homing(AxisMask cycle_mask) {
        // Do nothing.
        return false;
    }

    void Cartesian::kinematics_post_homing() {
        // Do nothing.
    }

    bool Cartesian::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        // Motor space is cartesian space, so we do no transform.
        return mc_move_motors(target, pl_data);
    }

    void Cartesian::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Motor space is cartesian space, so we do no transform.
        memcpy(cartesian, motors, n_axis * sizeof(motors[0]));
    }

    // Checks and reports if target array exceeds machine travel limits.
    // Return true if exceeding limits
    // Set $<axis>/MaxTravel=0 to selectively remove an axis from soft limit checks
    bool Cartesian::limitsCheckTravel(float* target) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            auto axisSetting = axes->_axis[axis];
            if ((target[axis] < limitsMinPosition(axis) || target[axis] > limitsMaxPosition(axis)) && axisSetting->_maxTravel > 0) {
                return true;
            }
        }
        return false;
    }

    void Cartesian::config_message() {
        log_info("Kinematic system: " << name());
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Cartesian> registration("Cartesian");
    }
}
