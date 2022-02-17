#include "Cartesian.h"

#include "../Machine/MachineConfig.h"

namespace Kinematics {
    void Cartesian::init() { log_info("Kinematic system: " << name()); }

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

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Cartesian> registration("Cartesian");
    }
}
