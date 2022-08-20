#include "Cartesian.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {
    void Cartesian::init() { log_info("Kinematic system: " << name()); }

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

    uint32_t Cartesian::homingMove(AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate) {
        float    maxSeekTime  = 0.0;
        float    limitingRate = 0.0;
        uint32_t settle       = 0;
        float    ratesq       = 0.0;

        //        log_debug("Cartesian homing " << int(axisMask) << " motors " << int(motors));

        auto  axes   = config->_axes;
        auto  n_axis = axes->_numberAxis;
        float rates[n_axis];

        bool seeking  = phase == Machine::Homing::Phase::FastApproach;
        bool approach = seeking || phase == Machine::Homing::Phase::SlowApproach;

        AxisMask axesMask = 0;
        // Find the axis that will take the longest
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_false(motors, Machine::Axes::motor_bit(axis, 0)) && bitnum_is_false(motors, Machine::Axes::motor_bit(axis, 1))) {
                continue;
            }

            // Record active axes for the next phase
            set_bitnum(axesMask, axis);

            // Set target location for active axes and setup computation for homing rate.
            set_motor_steps(axis, 0);

            auto axisConfig = axes->_axis[axis];
            auto homing     = axisConfig->_homing;

            settle = std::max(settle, homing->_settle_ms);

            float axis_rate;
            float travel;
            switch (phase) {
                case Machine::Homing::Phase::FastApproach:
                    axis_rate = homing->_seekRate;
                    travel    = axisConfig->_maxTravel;
                    break;
                case Machine::Homing::Phase::PrePulloff:
                case Machine::Homing::Phase::SlowApproach:
                case Machine::Homing::Phase::Pulloff0:
                case Machine::Homing::Phase::Pulloff1:
                    axis_rate = homing->_feedRate;
                    travel    = axisConfig->commonPulloff();
                    break;
                case Machine::Homing::Phase::Pulloff2:
                    axis_rate = homing->_feedRate;
                    travel    = axisConfig->extraPulloff();
                    if (travel < 0) {
                        // Motor0's pulloff is greater than motor1's, so we block motor1
                        axisConfig->_motors[1]->block();
                        travel = -travel;
                    } else if (travel > 0) {
                        // Motor1's pulloff is greater than motor0's, so we block motor0
                        axisConfig->_motors[0]->block();
                    }
                    // All motors will be unblocked later by set_homing_mode()
                    break;
            }

            // Accumulate the squares of the homing rates for later use
            // in computing the aggregate feed rate.
            ratesq += (axis_rate * axis_rate);

            // First we compute the maximum-time-to-completion vector; later we will
            // convert it back to positions after we determine the limiting axis.
            // Set target direction based on cycle mask and homing cycle approach state.
            auto seekTime = travel / axis_rate;

            target[axis] = (homing->_positiveDirection ^ approach) ? -travel : travel;
            rates[axis]  = axis_rate;

            if (seekTime > maxSeekTime) {
                maxSeekTime  = seekTime;
                limitingRate = axis_rate;
            }
        }
        // Scale the target array, currently in units of time, back to positions
        // When approaching add a fudge factor (scaler) to ensure that the limit is reached -
        // but no fudge factor when pulling off.
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axesMask, axis)) {
                auto homing = axes->_axis[axis]->_homing;
                auto scaler = approach ? (seeking ? homing->_seek_scaler : homing->_feed_scaler) : 1.0;
                target[axis] *= scaler;
                if (phase == Machine::Homing::Phase::FastApproach) {
                    // For fast approach the vector direction is determined by the rates
                    target[axis] *= rates[axis] / limitingRate;
                }
                // log_debug(axes->axisName(axis) << " target " << target[axis] << " rate " << rates[axis]);
            }
        }

        rate = sqrtf(ratesq);  // Magnitude of homing rate vector

        return settle;
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Cartesian> registration("Cartesian");
    }
}
