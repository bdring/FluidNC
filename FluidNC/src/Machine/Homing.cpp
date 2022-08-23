#include "Homing.h"

#include "../MotionControl.h"  // mc_reset
#include "../NutsBolts.h"      // set_bitnum, etc
#include "../System.h"         // sys.*
#include "../Stepper.h"        // st_wake
#include "../Protocol.h"       // protocol_handle_events
#include "../Limits.h"         // ambiguousLimit
#include "../Machine/Axes.h"
#include "../Machine/MachineConfig.h"  // config

#include <cmath>

namespace Machine {
    // Calculate the motion for the next homing move.
    //  Input: motors - the motors that should participate in this homing cycle
    //  Input: phase - one of PrePulloff, FastApproach, Pulloff0, SlowApproach, Pulloff1, Pulloff2
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

    Homing::Phase   Homing::_phase       = Phase::None;
    AxisMask        Homing::_cycleAxes   = 0;
    AxisMask        Homing::_phaseAxes   = 0;
    MotorMask       Homing::_cycleMotors = 0;
    MotorMask       Homing::_phaseMotors;
    std::queue<int> Homing::_remainingCycles;
    uint32_t        Homing::_settling_ms;

    const char* Homing::_phaseNames[] = {
        "None", "PrePulloff", "FastApproach", "Pulloff0", "SlowApproach", "Pulloff1", "Pulloff2", "CycleDone",
    };

    void Homing::startMove(float* target, float rate) {
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
        plan_data.feed_rate             = rate;  // Magnitude of homing rate vector

        plan_buffer_line(target, &plan_data);  // Bypass mc_move_motors(). Directly plan homing motion.

        //        sys.step_control                  = {};
        //        sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
        //        Stepper::prep_buffer();                    // Prep and fill segment buffer from newly planned block.
        //        Stepper::wake_up();                        // Initiate motion
        protocol_send_event(&cycleStartEvent);
    }

    static MotorMask limited() { return Machine::Axes::posLimitMask | Machine::Axes::negLimitMask; }

    void Homing::cycleStop() {
        log_debug("Homing cycleStop");
        if (approach()) {
            // Cycle stop while approaching means that we did not hit
            // a limit switch in the programmed distance
            fail(ExecAlarm::HomingFailApproach);
            return;
        }
        Machine::LimitPin::checkLimits();

        // Cycle stop in pulloff is success unless
        // the limit switches are still active.
        if (limited() & _phaseMotors) {
            // Homing failure: Limit switch still engaged after pull-off motion
            fail(ExecAlarm::HomingFailPulloff);
            return;
        }
        // Normal termination for pulloff cycle
        _phaseMotors = 0;

        // Advance to next cycle
        Stepper::reset();        // Stop steppers and reset step segment buffer
        delay_ms(_settling_ms);  // Delay to allow transient dynamics to dissipate.

        nextPhase();
    }

    void Homing::nextPhase() {
        _phase = static_cast<Phase>(static_cast<int>(_phase) + 1);
        log_debug("Homing nextPhase " << phaseName(_phase));
        if (_phase == CycleDone || (_phase == Phase::Pulloff2 && !needsPulloff2(_cycleMotors))) {
            set_mpos();
            nextCycle();
        } else {
            runPhase();
        }
    }

    void Homing::runPhase() {
        _phaseAxes   = _cycleAxes;
        _phaseMotors = _cycleMotors;

        if (_phase == Phase::PrePulloff) {
            Machine::LimitPin::checkLimits();
            if (!(limited() & _phaseMotors)) {
                // No initial pulloff needed
                nextPhase();
                return;
            }
        }

        if (approach()) {
            Machine::LimitPin::checkLimits();
        }

        float* target = get_mpos();
        float  rate;

        _settling_ms = config->_kinematics->homingMove(_phaseAxes, _phaseMotors, _phase, target, rate);

        log_debug("planned move to " << target[0] << "," << target[1] << "," << target[2] << "@" << rate);

        // Reset the limits so the motors will move
        clear_bits(Machine::Axes::posLimitMask, _cycleMotors);
        clear_bits(Machine::Axes::negLimitMask, _cycleMotors);

        startMove(target, rate);
    }

    void Homing::limitReached() {
        log_debug("Homing limit reached");

        if (!approach()) {
            // We are not supposed to see a limitReached event while pulling off
            fail(ExecAlarm::HomingFailPulloff);
            return;
        }

        // As limit bits are set, let the kinematics system figure out what that
        // means in terms of axes, motors, and whether to stop and replan
        MotorMask limited = Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;

        // log_debug("Homing limited 0x" << String(int(limited), 16););

        bool stop = config->_kinematics->limitReached(_phaseAxes, _phaseMotors, limited);

        // stop tells us whether we have to halt the motion and replan a new move to
        // complete the homing cycle for this set of axes.

        if (stop) {
            Stepper::reset();  // Stop moving

            if (_phaseAxes) {
                log_debug("Homing replan with axes" << int(_phaseAxes));
                // If there are any axes that have not yet hit their limits, replan with
                // the remaining axes.
                float* target = get_mpos();
                float  rate;
                _settling_ms = config->_kinematics->homingMove(_phaseAxes, _phaseMotors, _phase, target, rate);
                startMove(target, rate);
            } else {
                // If all axes have hit their limits, this phase is complete and
                // we can start the next one
                delay_ms(_settling_ms);  // Delay to allow transient dynamics to dissipate.
                nextPhase();
            }
        }
    }

    void Homing::done() {
        log_debug("Homing done");

        if (sys.abort) {
            return;  // Did not complete. Alarm state set by mc_alarm.
        }
        // Homing cycle complete! Setup system for normal operation.
        // -------------------------------------------------------------------------------------
        // Sync gcode parser and planner positions to homed position.
        gc_sync_position();
        plan_sync_position();

        Machine::LimitPin::checkLimits();
        config->_stepping->endLowLatency();

        if (!sys.abort) {             // Execute startup scripts after successful homing.
            sys.state = State::Idle;  // Set to IDLE when complete.
            Stepper::go_idle();       // Set steppers to the settings idle state before returning.
        }
    }

    void Homing::nextCycle() {
        // Start the next cycle in the queue
        if (sys.state == State::Alarm) {
            while (!_remainingCycles.empty()) {
                _remainingCycles.pop();
            }
            return;
        }
        if (_remainingCycles.empty()) {
            done();
            return;
        }
        _cycleAxes = _remainingCycles.front();
        _remainingCycles.pop();

        log_debug("Homing Cycle axes " << int(_cycleAxes));

        _cycleAxes &= Machine::Axes::homingMask;
        _cycleMotors = config->_axes->set_homing_mode(_cycleAxes, true);

        _phase = Phase::PrePulloff;
        runPhase();
    }

    void Homing::fail(ExecAlarm alarm) {
        Stepper::reset();  // Stop moving
        Machine::LimitPin::checkLimits();
        rtAlarm = alarm;
        config->_axes->set_homing_mode(_cycleAxes, false);  // tell motors homing is done...failed
    }

    bool Homing::needsPulloff2(MotorMask motors) {
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

            // check to see if the axis has different pulloffs for its motors
            if (axes->_axis[axis]->extraPulloff()) {
                return true;
            }
        }

        return false;
    }

    // Homes the specified cycle axes, sets the machine position, and performs a pull-off motion after
    // completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
    // the trigger point of the limit switches. The rapid stops are handled by a system level axis lock
    // mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically
    // circumvent the processes for executing motions in normal operation.
    // NOTE: Only the abort realtime command can interrupt this process.

    // axes cannot be 0.  The 0 case - run all cycles - is
    // handled by the caller mc_homing_cycle()

    void Homing::set_mpos() {
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
            if (bitnum_is_true(_cycleAxes, axis)) {
                set_motor_steps(axis, mpos_to_steps(axes->_axis[axis]->_homing->_mpos, axis));
            }
        }
        sys.step_control = {};                     // Return step control to normal operation.
        axes->set_homing_mode(_cycleAxes, false);  // tell motors homing is done
    }

    static String axisNames(AxisMask axisMask) {
        String retval = "";
        auto   n_axis = config->_axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                retval += Machine::Axes::_names[axis];
            }
        }
        return retval;
    }

    // Construct a list of homing cycles to run.  If there are any
    // such cycles, enter Homing state and begin running the first
    // cycle.  The protocol loop will then respond to events and advance
    // the homing state machine through its phases.
    void Homing::run_cycles(AxisMask axisMask) {
        if (!config->_kinematics->canHome(axisMask)) {
            log_error("This kinematic system cannot do homing");
            sys.state = State::Alarm;
            return;
        }

        while (!_remainingCycles.empty()) {
            _remainingCycles.pop();
        }

        if (axisMask != AllCycles) {
            _remainingCycles.push(axisMask);
        } else {
            // Run all homing cycles
            bool someAxisHomed = false;

            for (int cycle = 1; cycle <= MAX_N_AXIS; cycle++) {
                // Set axisMask to the axes that home on this cycle
                axisMask = axis_mask_from_cycle(cycle);

                if (axisMask) {  // if there are some axes in this cycle
                    _remainingCycles.push(axisMask);
                }
            }
        }

        if (_remainingCycles.empty()) {
            log_error("No homing cycles defined");
            sys.state = State::Alarm;
            return;
        }
        config->_stepping->beginLowLatency();

        sys.state = State::Homing;
        nextCycle();
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
