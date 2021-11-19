#include "Homing.h"

#include "../MotionControl.h"  // mc_reset
#include "../NutsBolts.h"      // set_bitnum, etc
#include "../System.h"         // sys.*
#include "../Stepper.h"        // st_wake
#include "../Protocol.h"       // protocol_execute_realtime
#include "../Machine/Axes.h"
#include "../Machine/MachineConfig.h"  // config

#include <cmath>

namespace Machine {
    // Calculate the motion for the next homing move.
    //  Input: axesMask - the axes that should participate in this homing cycle
    //  Input: approach - the direction of motion - true for approach, false for pulloff
    //  Input: seek - the phase - true for the initial high-speed seek, false for the slow second phase
    //  Output: axislock - the axes that actually participate, accounting
    //  Output: target - the endpoint vector of the motion
    //  Output: rate - the feed rate
    //  Return: settle - the maximum delay time of all the axes

    // For multi-axis homing, we use the per-axis rates and travel limits to compute
    // a target vector and a feedrate as follows:
    // The goal is for each axis to travel at its specified rate, and for the
    // maximum travel to be enough for each participating axis to reach its limit.
    // For the rate goal, the axis components of the target vector must be proportional
    // to the per-axis rates, and the overall feed rate must be the magnitude of the
    // vector of per-axis rates.
    // For the travel goal, the axis components of the target vector must be scaled
    // according to the one that would take the longest.
    // The time to complete a maxTravel move for a given feedRate is maxTravel/feedRate.
    // We compute that time for all axes in the homing cycle, then find the longest one.
    // Then we scale the travel distances for the other axes so they would complete
    // at the same time.

    const uint32_t MOTOR0 = 0xffff;
    const uint32_t MOTOR1 = 0xffff0000;

    // The return value is the setting time
    uint32_t Homing::plan_move(MotorMask motors, bool approach, bool seek, float customPulloff) {
        float    maxSeekTime  = 0.0;
        float    limitingRate = 0.0;
        uint32_t settle       = 0;
        float    rate         = 0.0;

        auto   axes   = config->_axes;
        auto   n_axis = axes->_numberAxis;
        float* target = get_mpos();

        AxisMask axesMask = 0;
        // Find the axis that will take the longest
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_false(motors, axis) && bitnum_is_false(motors, axis + 16)) {
                continue;
            }

            // Record active axes for the next phase
            set_bitnum(axesMask, axis);

            // Set target location for active axes and setup computation for homing rate.
            motor_steps[axis] = 0;

            auto axisConfig = axes->_axis[axis];
            auto homing     = axisConfig->_homing;

            settle = std::max(settle, homing->_settle_ms);

            float axis_rate = seek ? homing->_seekRate : homing->_feedRate;

            // Accumulate the squares of the homing rates for later use
            // in computing the aggregate feed rate.
            rate += (axis_rate * axis_rate);

            float travel = 0.0;
            if (seek) {
                travel = axisConfig->_maxTravel;
            } else {  // see if which pull off we use
                // If both motors are running use the first pull off
                if (bitnum_is_true(motors, axis) && bitnum_is_true(motors, axis + 16)) {
                    travel = axisConfig->_motors[0]->_pulloff;
                } else {
                    if (customPulloff != 0) {
                        travel = customPulloff;
                    } else {
                        bitnum_is_true(motors, axis) ? travel = axisConfig->_motors[0]->_pulloff : travel = axisConfig->_motors[1]->_pulloff;
                    }
                }
            }

            // First we compute the maximum-time-to-completion vector; later we will
            // convert it back to positions after we determine the limiting axis.
            // Set target direction based on cycle mask and homing cycle approach state.
            auto seekTime = travel / axis_rate;

            target[axis] = (homing->_positiveDirection ^ approach) ? -seekTime : seekTime;
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
                auto scaler = approach ? (seek ? homing->_seek_scaler : homing->_feed_scaler) : 1.0;
                target[axis] *= limitingRate * scaler;
            }
        }

        plan_line_data_t plan_data;
        plan_data.spindle_speed         = 0;
        plan_data.motion                = {};
        plan_data.motion.systemMotion   = 1;
        plan_data.motion.noFeedOverride = 1;
        plan_data.spindle               = SpindleState::Disable;
        plan_data.coolant.Mist          = 0;
        plan_data.coolant.Flood         = 0;
        plan_data.line_number           = REPORT_LINE_NUMBER;
        plan_data.is_jog                = false;

        plan_data.feed_rate = float(sqrt(rate));  // Magnitude of homing rate vector
        plan_buffer_line(target, &plan_data);     // Bypass mc_move_motors(). Directly plan homing motion.

        sys.step_control                  = {};
        sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
        Stepper::prep_buffer();                    // Prep and fill segment buffer from newly planned block.
        Stepper::wake_up();                        // Initiate motion

        return settle;
    }

    void Homing::run(MotorMask remainingMotors, bool approach, bool seek, float customPulloff = 0) {
        // See if any motors are left.  This could be 0 if none of the motors specified
        // by the original value of axes is capable of standard homing.
        if (remainingMotors == 0) {
            return;
        }

        uint32_t settling_ms = plan_move(remainingMotors, approach, seek, customPulloff);

        config->_axes->lock_motors(0xffffffff);
        config->_axes->unlock_motors(remainingMotors);

        do {
            if (approach) {
                // Check limit state. Lock out cycle axes when they change.
                // XXX do we check only the switch in the direction of motion?
                MotorMask limitedMotors = Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;

                config->_axes->lock_motors(limitedMotors);
                clear_bits(remainingMotors, limitedMotors);
            }

            Stepper::prep_buffer();  // Check and prep segment buffer.

            // This checks some of the events that would normally be handled
            // by protocol_execute_realtime().  The homing loop is time-critical
            // so we handle those events directly here, calling protocol_execute_realtime()
            // only if one of those events is active.
            if (rtReset) {
                // Homing failure: Reset issued during cycle.
                throw ExecAlarm::HomingFailReset;
            }
            if (rtSafetyDoor) {
                // Homing failure: Safety door was opened.
                throw ExecAlarm::HomingFailDoor;
            }
            if (rtCycleStop) {
                rtCycleStop = false;
                if (approach) {
                    // Homing failure: Limit switch not found during approach.
                    throw ExecAlarm::HomingFailApproach;
                }
                // Pulloff
                if ((Machine::Axes::posLimitMask | Machine::Axes::negLimitMask) & remainingMotors) {
                    // Homing failure: Limit switch still engaged after pull-off motion
                    throw ExecAlarm::HomingFailPulloff;
                }
                // Normal termination for pulloff cycle
                remainingMotors = 0;
            }
            pollChannels();
        } while (remainingMotors);

        Stepper::reset();       // Immediately force kill steppers and reset step segment buffer.
        delay_ms(settling_ms);  // Delay to allow transient dynamics to dissipate.
    }

    // This homing mode is the POG style squaring
    // For this you can only have switches defined at the axis level
    bool Homing::squaredOneSwitch(MotorMask motors) {
        AxisMask squaredAxes = motors & (motors >> 16);
        if (squaredAxes == 0) {
            // No axis has multiple motors
            return false;
        }

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_false(squaredAxes, axis)) {
                continue;
            }

            if (axes->_axis[axis]->motorsWithSwitches() == 1) {
                return true;
            }
        }
        // If we get here, all of the squared axes in this cycle have separate
        // limit switches.
        return false;
    }

    // This homing mode never forces the machine out of square
    // This requires independent switches on each motor.
    bool Homing::squaredStressfree(MotorMask motors) {
        AxisMask squaredAxes = motors & (motors >> 16);
        if (squaredAxes == 0) {
            // No axis has multiple motors
            return false;
        }

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_false(squaredAxes, axis)) {
                continue;
            }

            // check to see if either side is missing a switch
            if (axes->_axis[axis]->motorsWithSwitches() != 2) {
                return false;
            }
        }

        return true;
    }

    // Homes the specified cycle axes, sets the machine position, and performs a pull-off motion after
    // completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
    // the trigger point of the limit switches. The rapid stops are handled by a system level axis lock
    // mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically
    // circumvent the processes for executing motions in normal operation.
    // NOTE: Only the abort realtime command can interrupt this process.

    // axes cannot be 0.  The 0 case - run all cycles - is
    // handled by the caller mc_homing_cycle()

    void Homing::set_mpos(AxisMask axisMask) {
        // The active cycle axes should now be homed and machine limits have been located. By
        // default, as with most CNCs, machine space is all negative, but that can be changed.
        // Since limit switches
        // can be on either side of an axes, check and set axes machine zero appropriately. Also,
        // set up pull-off maneuver from axes limit switches that have been homed. This provides
        // some initial clearance off the switches and should also help prevent them from falsely
        // triggering when hard limits are enabled or when more than one axes shares a limit pin.

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;

        // Set machine positions for homed limit switches. Don't update non-homed axes.
        for (int axis = 0; axis < n_axis; axis++) {
            Machine::Axis* axisConf = config->_axes->_axis[axis];
            auto           homing   = axisConf->_homing;

            if (bitnum_is_true(axisMask, axis)) {
                auto mpos    = homing->_mpos;
                auto pulloff = axisConf->_motors[0]->_pulloff;

                mpos += homing->_positiveDirection ? -pulloff : pulloff;
                motor_steps[axis] = mpos_to_steps(mpos, axis);
            }
        }
        sys.step_control = {};                            // Return step control to normal operation.
        config->_axes->set_homing_mode(axisMask, false);  // tell motors homing is done
    }

    void Homing::run_one_cycle(AxisMask axisMask) {
        axisMask &= Machine::Axes::homingMask;
        MotorMask motors = config->_axes->set_homing_mode(axisMask, true);

        try {
            if (squaredOneSwitch(motors)) {
                //log_info("POG Squaring");
                run(motors, true, true);             // Approach slowly
                run(motors, false, false);           // Pulloff
                run(motors & MOTOR0, true, false);   // Approach slowly
                run(motors & MOTOR0, false, false);  // Pulloff
                run(motors & MOTOR1, true, false);   // Approach slowly
                run(motors & MOTOR1, false, false);  // Pulloff
            } else if (squaredStressfree(motors)) {
                //log_info("Stress Free Squaring 0x" << String(motors, 16));
                run(motors, true, true);    // Approach fast
                run(motors, false, false);  // Pulloff
                run(motors, true, false);   // Approach fast
                run(motors, false, false);  // Pulloff
                // TODO See if we need to do a nudge for asymmetric pulloffs.
                auto  n_axis = config->_axes->_numberAxis;
                float pulloffOffset;
                for (int axis = 0; axis < n_axis; axis++) {
                    if (bitnum_is_true(motors, axis)) {
                        auto axisConfig = config->_axes->_axis[axis];
                        if (axisConfig->hasDualMotor()) {
                            pulloffOffset = axisConfig->pulloffOffset();
                            if (pulloffOffset != 0) {
                                //log_info("Pulloff offset needed on axis " << axis << " of " << pulloffOffset);
                                // TODO Do it
                                run(motors & MOTOR1, false, false, pulloffOffset);
                            }
                        }
                    }
                }
            } else {
                //log_info("Single Axis Homing");
                for (int i = 0; i < NHomingLocateCycle; i++) {
                    run(motors, true, true);    // Approach fast
                    run(motors, false, false);  // Pulloff
                    run(motors, true, false);   // Approach slowly
                    run(motors, false, false);  // Pulloff
                }
            }
        } catch (ExecAlarm alarm) {
            rtAlarm = alarm;
            config->_axes->set_homing_mode(axisMask, false);  // tell motors homing is done...failed
            log_error("Homing fail");
            mc_reset();  // Stop motors, if they are running.
            // protocol_execute_realtime() will handle any pending rtXXX conditions
            protocol_execute_realtime();
            return;
        }

        set_mpos(axisMask);
    }

    // XXX this should return Error or at least set ExecAlarm::HomingNoCycles (not yet defined)
    void Homing::run_cycles(AxisMask axisMask) {
        // -------------------------------------------------------------------------------------
        // Perform homing routine. NOTE: Special motion case. Only system reset works.
        if (axisMask != AllCycles) {
            run_one_cycle(axisMask);
        } else {
            // Run all homing cycles
            bool someAxisHomed = false;

            for (int cycle = 1; cycle <= MAX_N_AXIS; cycle++) {
                // Set axisMask to the axes that home on this cycle

                axisMask = axis_mask_from_cycle(cycle);

                // axisMask    = 0;
                // auto n_axis = config->_axes->_numberAxis;
                // for (int axis = 0; axis < n_axis; axis++) {
                //     auto axisConfig = config->_axes->_axis[axis];
                //     auto homing     = axisConfig->_homing;
                //     if (homing && homing->_cycle == cycle) {
                //         set_bitnum(axisMask, axis);
                //     }
                // }

                if (axisMask) {  // if there are some axes in this cycle
                    someAxisHomed = true;
                    run_one_cycle(axisMask);
                }
            }
            if (!someAxisHomed) {
                log_error("No homing cycles defined");
                sys.state = State::Alarm;
            }
        }
    }

    AxisMask Homing::axis_mask_from_cycle(int cycle) {
        AxisMask axisMask = 0;
        auto     n_axis   = config->_axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            auto axisConfig = config->_axes->_axis[axis];
            auto homing     = axisConfig->_homing;
            if (homing && homing->_cycle == cycle) {
                set_bitnum(axisMask, axis);
            }
        }
        return axisMask;
    }
}
