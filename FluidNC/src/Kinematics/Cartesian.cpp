#include "Cartesian.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {
    void Cartesian::init() {
        log_info("Kinematic system: " << name());
    }

    bool Cartesian::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        // Motor space is cartesian space, so we do no transform.
        return mc_move_motors(target, pl_data);
    }

    void Cartesian::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        // Motor space is cartesian space, so we do no transform.
        copyAxes(cartesian, motors);
    }

    bool Cartesian::canHome(AxisMask axisMask) {
        if (ambiguousLimit()) {
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return false;
        }
        return true;
    }

    bool Cartesian::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
        // For Cartesian, the limit switches are associated with individual motors, since
        // an axis can have dual motors each with its own limit switch.  We clear the motors in
        // the mask whose limits have been reached.
        clear_bits(motorMask, limited);

        // Set axisMask according to the motors that are still running.
        axisMask = Machine::Axes::motors_to_axes(motorMask);

        // We do not have to stop until all motors have reached their limits
        return !axisMask;
    }

    bool Cartesian::homingMove(
        AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate, uint32_t& settle_ms) {
        Machine::Homing::axisVector(axisMask, motors, phase, target, rate, settle_ms);
        // Turn off axis limits in case they were set by a limit switch

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                // Homing moves are calculated as though they start from 0
                set_motor_steps(axis, 0);

                auto paxis = axes->_axis[axis];

                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 0))) {
                    paxis->_motors[0]->unlimit();
                }
                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 1))) {
                    paxis->_motors[1]->unlimit();
                }
            }
        }

        return true;
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Cartesian> registration("Cartesian");
    }
}
