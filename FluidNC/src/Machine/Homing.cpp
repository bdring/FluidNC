// Copyright (c) 2021 - Stefan de Bruijn, Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Homing.h"

#include "System.h"    // sys.*
#include "Stepper.h"   // st_wake
#include "Protocol.h"  // protocol_handle_events
#include "Limit.h"     // ambiguousLimit
#include "Machine/Axes.h"
#include "Machine/MachineConfig.h"  // config

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

    Homing::Phase   Homing::_phase         = Phase::None;
    AxisMask        Homing::_cycleAxes     = 0;
    AxisMask        Homing::_phaseAxes     = 0;
    AxisMask        Homing::direction_mask = 0;
    MotorMask       Homing::_cycleMotors   = 0;
    MotorMask       Homing::_phaseMotors;
    std::queue<int> Homing::_remainingCycles;
    uint32_t        Homing::_settling_ms;

    uint32_t Homing::_runs;

    AxisMask Homing::_unhomed_axes = 0;  // Bitmap of axes whose position is unknown

    bool Homing::axis_is_homed(axis_t axis) {
        return bitnum_is_false(_unhomed_axes, axis);
    }
    void Homing::set_axis_homed(axis_t axis) {
        clear_bitnum(_unhomed_axes, axis);
    }
    void Homing::set_axis_unhomed(axis_t axis) {
        set_bitnum(_unhomed_axes, axis);
    }
    void Homing::set_all_axes_unhomed() {
        if (config->_start->_mustHome) {
            _unhomed_axes = Machine::Axes::homingMask;
        }
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

        if (_phase == SlowApproach && _runs == 1) {
            // If this is the last approach/pulloff run, skip past the Pulloff1 phase
            _phase = Pulloff2;
        } else if (_phase == Pulloff2 && --_runs > 1) {
            // If we haven't done all of the runs, go back to the SlowApproach phase
            _phase = SlowApproach;
        }

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

        config->_kinematics->homing_move(_phaseAxes, _phaseMotors, _phase, _settling_ms);
    }

    void Homing::limitReached() {
        // As limit bits are set, let the kinematics system figure out what that
        // means in terms of axes, motors, and whether to stop and replan
        MotorMask limited = Machine::Axes::posLimitMask | Machine::Axes::negLimitMask;

        if (!approach()) {
            // Ignore limit switch chatter while pulling off
            return;
        }

        log_debug("Homing limited" << Axes::motorMaskToNames(limited));

        // limitReached modifies _phaseAxes and _phaseMotors according to the value of limited
        // It returns a flag that is true if the cycle is complete
        bool stop = config->_kinematics->limitReached(_phaseAxes, _phaseMotors, limited);

        // stop tells us whether we have to halt the motion and replan a new move to
        // complete the homing cycle for this set of axes.

        if (stop) {
            Stepper::reset();  // Stop moving

            if (_phaseAxes) {
                log_debug("Homing replan with " << Axes::maskToNames(_phaseAxes));

                // If there are any axes that have not yet hit their limits, replan with
                // the remaining axes.
                config->_kinematics->homing_move(_phaseAxes, _phaseMotors, _phase, _settling_ms);
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

        if (sys.abort()) {
            return;  // Did not complete. Alarm state set by mc_alarm.
        }
        // Homing cycle complete! Setup system for normal operation.
        // -------------------------------------------------------------------------------------
        // Sync gcode parser and planner positions to homed position.
        gc_sync_position();
        plan_sync_position();

        Stepping::endLowLatency();

        if (!sys.abort()) {
            set_state(unhomed_axes() ? State::Alarm : State::Idle);
            Stepper::go_idle();  // Set steppers to the settings idle state before returning.
            if (state_is(State::Idle)) {
                config->_macros->_after_homing.run(&allChannels);
            }
        }
    }

    void Homing::nextCycle() {
        // Start the next cycle in the queue
        if (state_is(State::Alarm)) {
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

        log_debug("Homing Cycle " << Axes::maskToNames(_cycleAxes));

        _cycleAxes &= Machine::Axes::homingMask;
        _cycleMotors = Axes::set_homing_mode(_cycleAxes, true);

        _phase = Phase::PrePulloff;
        _runs  = Axes::_homing_runs;
        runPhase();
    }

    void Homing::fail(ExecAlarm alarm) {
        Stepper::reset();  // Stop moving
        send_alarm(alarm);
        Axes::set_homing_mode(_cycleAxes, false);  // tell motors homing is done...failed
        Axes::set_disable(Stepping::_idleMsecs != 255);
    }

    bool Homing::needsPulloff2(MotorMask motors) {
        AxisMask squaredAxes = motors & (motors >> 16);
        if (squaredAxes == 0) {
            // No axis has multiple motors
            return false;
        }

        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
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

        float*      mpos = get_mpos();
        std::string homedAxes;
        //        logArray("mpos was", mpos, n_axis);
        // Replace coordinates homed axes with the homing values.
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
            if (bitnum_is_true(_cycleAxes, axis)) {
                auto homing = axes->_axis[axis]->_homing;
                if (homing) {
                    set_axis_homed(axis);
                    mpos[axis] = homing->_mpos;
                    homedAxes += axes->axisName(axis);
                }
            }
        }
        log_msg("Homed:" << homedAxes);
        //        logArray("mpos becomes", mpos, n_axis);

        config->_kinematics->set_homed_mpos(mpos);

        mpos = get_mpos();
        //        logArray("mpos transformed", mpos, n_axis);

        sys.step_control = {};                     // Return step control to normal operation.
        axes->set_homing_mode(_cycleAxes, false);  // tell motors homing is done
    }

#if 0
    static std::string axisNames(AxisMask axisMask) {
        std::string retval = "";
        auto        n_axis = Axes::_numberAxis;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
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
            set_state(State::Alarm);
            return;
        }

        // Find any cycles that set the m_pos without motion
        auto n_axis = Axes::_numberAxis;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
            auto homing = Axes::_axis[axis]->_homing;
            if (homing && homing->_cycle == set_mpos_only) {
                if (axisMask == 0 || axisMask & 1 << axis) {
                    float* mpos = get_mpos();
                    mpos[axis]  = homing->_mpos;
                    config->_kinematics->set_homed_mpos(mpos);
                    if (axisMask == bitnum_to_mask(axis)) {
                        return;
                    }

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
            set_state(State::Alarm);
            return;
        }
        Stepping::beginLowLatency();

        set_state(State::Homing);
        nextCycle();
    }

    AxisMask Homing::axis_mask_from_cycle(uint32_t cycle) {
        AxisMask axisMask = 0;
        auto     n_axis   = Axes::_numberAxis;
        for (axis_t axis = X_AXIS; axis < n_axis; axis++) {
            auto axisConfig = Axes::_axis[axis];
            auto homing     = axisConfig->_homing;
            if (homing && homing->_cycle == cycle) {
                set_bitnum(axisMask, axis);
            }
        }
        return axisMask;
    }
}
