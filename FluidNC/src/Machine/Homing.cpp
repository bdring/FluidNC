#include "Homing.h"

#include "../System.h"                 // sys.*
#include "../Stepper.h"                // st_wake
#include "../Protocol.h"               // protocol_handle_events
#include "../Limits.h"                 // ambiguousLimit
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

    AxisMask Homing::_unhomed_axes;  // Bitmap of axes whose position is unknown

    bool Homing::axis_is_homed(size_t axis) {
        return bitnum_is_false(_unhomed_axes, axis);
    }
    void Homing::set_axis_homed(size_t axis) {
        clear_bitnum(_unhomed_axes, axis);
    }
    void Homing::set_axis_unhomed(size_t axis) {
        set_bitnum(_unhomed_axes, axis);
    }
    void Homing::set_all_axes_unhomed() {
        _unhomed_axes = Machine::Axes::homingMask;
    }
    void Homing::set_all_axes_homed() {
        _unhomed_axes = 0;
    }

    AxisMask Homing::unhomed_axes() {
        return _unhomed_axes;
    }

    const char* Homing::_phaseNames[] = {
        "None", "PrePulloff", "FastApproach", "Pulloff0", "SlowApproach", "Pulloff1", "Pulloff2", "CycleDone",
    };

    void Homing::startMove(AxisMask axisMask, MotorMask motors, Phase phase, uint32_t& settle_ms) {
        float rate;
        float target[config->_axes->_numberAxis];
        axisVector(_phaseAxes, _phaseMotors, _phase, target, rate, _settling_ms);

        plan_line_data_t plan_data      = {};
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

        config->_kinematics->cartesian_to_motors(target, &plan_data, get_mpos());

        protocol_send_event(&cycleStartEvent);
    }

    static MotorMask limited() {
        return Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;
    }

    void Homing::cycleStop() {
        log_debug("CycleStop " << phaseName(_phase));
        if (approach()) {
            // Cycle stop while approaching means that we did not hit
            // a limit switch in the programmed distance
            fail(ExecAlarm::HomingFailApproach);
            report_realtime_status(allChannels);
            return;
        }

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

    void Homing::axisVector(AxisMask axisMask, MotorMask motors, Machine::Homing::Phase phase, float* target, float& rate, uint32_t& settle_ms) {
        copyAxes(target, get_mpos());

        log_debug("Starting from " << target[0] << "," << target[1] << "," << target[2]);

        float maxSeekTime = 0.0;
        float ratesq      = 0.0;

        settle_ms = 0;

        //        log_debug("Cartesian homing " << int(axisMask) << " motors " << int(motors));

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;

        float rates[n_axis]    = { 0 };
        float distance[n_axis] = { 0 };

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

            auto axisConfig = axes->_axis[axis];
            auto homing     = axisConfig->_homing;

            settle_ms = std::max(settle_ms, homing->_settle_ms);

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

            // Set target direction based on various factors
            switch (phase) {
                case Machine::Homing::Phase::PrePulloff: {
                    // For PrePulloff, the motion depends on which switches are active.
                    MotorMask axisMotors = Machine::Axes::axes_to_motors(1 << axis);
                    bool      posLimited = bits_are_true(Machine::Axes::posLimitMask, axisMotors);
                    bool      negLimited = bits_are_true(Machine::Axes::negLimitMask, axisMotors);
                    if (posLimited && negLimited) {
                        log_error("Both positive and negative limit switches are active for axis " << axes->axisName(axis));
                        // xxx need to abort somehow
                        return;
                    }
                    if (posLimited) {
                        distance[axis] = -travel;
                    } else if (negLimited) {
                        distance[axis] = travel;
                    } else {
                        distance[axis] = 0;
                    }
                } break;

                case Machine::Homing::Phase::FastApproach:
                case Machine::Homing::Phase::SlowApproach:
                    distance[axis] = homing->_positiveDirection ? travel : -travel;
                    break;

                case Machine::Homing::Phase::Pulloff0:
                case Machine::Homing::Phase::Pulloff1:
                case Machine::Homing::Phase::Pulloff2:
                    distance[axis] = homing->_positiveDirection ? -travel : travel;
                    break;
            }

            // Accumulate the squares of the homing rates for later use
            // in computing the aggregate feed rate.
            ratesq += (axis_rate * axis_rate);

            rates[axis] = axis_rate;

            auto seekTime = travel / axis_rate;
            if (seekTime > maxSeekTime) {
                maxSeekTime = seekTime;
            }
        }

        // When approaching add a fudge factor (scaler) to ensure that the limit is reached -
        // but no fudge factor when pulling off.
        // For fast approach, scale the distance array according to the axis that will
        // take the longest time to reach its max range at its seek rate, preserving
        // the speeds of the axes.

        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axesMask, axis)) {
                if (phase == Machine::Homing::Phase::FastApproach) {
                    // For fast approach the vector direction is determined by the rates
                    float absDistance = maxSeekTime * rates[axis];
                    distance[axis]    = distance[axis] >= 0 ? absDistance : -absDistance;
                }

                auto paxis  = axes->_axis[axis];
                auto homing = paxis->_homing;
                auto scaler = approach ? (seeking ? homing->_seek_scaler : homing->_feed_scaler) : 1.0;
                distance[axis] *= scaler;
                target[axis] += distance[axis];
            }
        }

        rate = sqrtf(ratesq);  // Magnitude of homing rate vector
        log_debug("Planned move to " << target[0] << "," << target[1] << "," << target[2] << " @ " << rate);
    }

    void Homing::runPhase() {
        _phaseAxes   = _cycleAxes;
        _phaseMotors = _cycleMotors;

        // _phaseMotors can be 0 if set_homing_mode() either rejected all the
        // motors or handled them independently.  In that case we do not have
        // to run a conventional move-to-limit cycle.  Just skip to the end.
        if (!_phaseMotors) {
            _phase = static_cast<Phase>(static_cast<int>(Phase::CycleDone) - 1);
            nextPhase();
            return;
        }

        if (_phase == Phase::PrePulloff) {
            if (!(limited() & _phaseMotors)) {
                // No initial pulloff needed
                nextPhase();
                return;
            }
        }

        config->_kinematics->releaseMotors(_phaseAxes, _phaseMotors);

        startMove(_phaseAxes, _phaseMotors, _phase, _settling_ms);
    }

    void Homing::limitReached() {
        // As limit bits are set, let the kinematics system figure out what that
        // means in terms of axes, motors, and whether to stop and replan
        MotorMask limited = Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;

        if (!approach()) {
            // Ignore limit switch chatter while pulling off
            return;
        }

        log_debug("Homing limited" << config->_axes->motorMaskToNames(limited));

        bool stop = config->_kinematics->limitReached(_phaseAxes, _phaseMotors, limited);

        // stop tells us whether we have to halt the motion and replan a new move to
        // complete the homing cycle for this set of axes.

        if (stop) {
            Stepper::reset();  // Stop moving

            if (_phaseAxes) {
                log_debug("Homing replan with " << config->_axes->maskToNames(_phaseAxes));

                config->_kinematics->releaseMotors(_phaseAxes, _phaseMotors);

                // If there are any axes that have not yet hit their limits, replan with
                // the remaining axes.
                startMove(_phaseAxes, _phaseMotors, _phase, _settling_ms);
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

        config->_stepping->endLowLatency();

        if (!sys.abort) {
            sys.state = unhomed_axes() ? State::Alarm : State::Idle;
            Stepper::go_idle();  // Set steppers to the settings idle state before returning.
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

        log_debug("Homing Cycle " << config->_axes->maskToNames(_cycleAxes));

        _cycleAxes &= Machine::Axes::homingMask;
        _cycleMotors = config->_axes->set_homing_mode(_cycleAxes, true);

        _phase = Phase::PrePulloff;
        runPhase();
    }

    void Homing::fail(ExecAlarm alarm) {
        Stepper::reset();                                   // Stop moving
        send_alarm(alarm);
        config->_axes->set_homing_mode(_cycleAxes, false);  // tell motors homing is done...failed
        config->_axes->set_disable(config->_stepping->_idleMsecs != 255);
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

    void Homing::set_mpos() {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;

        float* mpos = get_mpos();

        log_debug("mpos was " << mpos[0] << "," << mpos[1] << "," << mpos[2]);
        // Replace coordinates homed axes with the homing values.
        for (size_t axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(_cycleAxes, axis)) {
                set_axis_homed(axis);
                mpos[axis] = axes->_axis[axis]->_homing->_mpos;
            }
        }
        log_debug("mpos becomes " << mpos[0] << "," << mpos[1] << "," << mpos[2]);

        set_motor_steps_from_mpos(mpos);

        mpos = get_mpos();
        log_debug("mpos transformed " << mpos[0] << "," << mpos[1] << "," << mpos[2]);

        sys.step_control = {};                     // Return step control to normal operation.
        axes->set_homing_mode(_cycleAxes, false);  // tell motors homing is done
    }

#if 0
    static std::string axisNames(AxisMask axisMask) {
        std::string retval = "";
        auto        n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                retval += Machine::Axes::_names[axis];
            }
        }
        return retval;
    }
#endif

    // Construct a list of homing cycles to run.  If there are any
    // such cycles, enter Homing state and begin running the first
    // cycle.  The protocol loop will then respond to events and advance
    // the homing state machine through its phases.
    void Homing::run_cycles(AxisMask axisMask) {
        // Check to see if the Kinematics takes care of homing.
        if (config->_kinematics->kinematics_homing(axisMask)) {
            return;
        }

        if (!config->_kinematics->canHome(axisMask)) {
            sys.state = State::Alarm;
            return;
        }

        // Find any cycles that set the m_pos without motion
        auto n_axis = config->_axes->_numberAxis;
        for (int axis = X_AXIS; axis < n_axis; axis++) {
            if (config->_axes->_axis[axis]->_homing->_cycle == set_mpos_only) {
                if (axisMask == 0 || axisMask & 1 << axis) {
                    float* mpos = get_mpos();
                    mpos[axis]  = config->_axes->_axis[axis]->_homing->_mpos;
                    set_motor_steps_from_mpos(mpos);
                    if (axisMask == bitnum_to_mask(axis))
                        return;

                    clear_bitnum(axisMask, axis);
                }
            }
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
