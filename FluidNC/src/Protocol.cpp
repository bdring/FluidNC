// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Protocol.cpp - execution state machine
*/

#include "Protocol.h"
#include "Event.h"

#include "Machine/MachineConfig.h"
#include "Machine/Homing.h"
#include "Report.h"         // report_feedback_message
#include "Limits.h"         // limits_get_state, soft_limit
#include "Planner.h"        // plan_get_current_block
#include "MotionControl.h"  // PARKING_MOTION_LINE_NUMBER
#include "Settings.h"       // settings_execute_startup
#include "InputFile.h"      // infile
#include "WebUI\Commands.h"
#include "Logging.h"
#include "Machine/LimitPin.h"

volatile ExecAlarm rtAlarm;  // Global realtime executor bitflag variable for setting various alarms.

std::map<ExecAlarm, const char*> AlarmNames = {
    { ExecAlarm::None, "None" },
    { ExecAlarm::HardLimit, "Hard Limit" },
    { ExecAlarm::SoftLimit, "Soft Limit" },
    { ExecAlarm::AbortCycle, "Abort Cycle" },
    { ExecAlarm::ProbeFailInitial, "Probe Fail Initial" },
    { ExecAlarm::ProbeFailContact, "Probe Fail Contact" },
    { ExecAlarm::HomingFailReset, "Homing Fail Reset" },
    { ExecAlarm::HomingFailDoor, "Homing Fail Door" },
    { ExecAlarm::HomingFailPulloff, "Homing Fail Pulloff" },
    { ExecAlarm::HomingFailApproach, "Homing Fail Approach" },
    { ExecAlarm::SpindleControl, "Spindle Control" },
    { ExecAlarm::ControlPin, "Control Pin Initially On" },
    { ExecAlarm::HomingAmbiguousSwitch, "Ambiguous Switch" },
};

volatile bool rtReset;

static volatile bool rtSafetyDoor;

volatile bool runLimitLoop;  // Interface to show_limits()

static void protocol_exec_rt_suspend();

static char line[LINE_BUFFER_SIZE];     // Line to be executed. Zero-terminated.
static char comment[LINE_BUFFER_SIZE];  // Line to be executed. Zero-terminated.
// static uint8_t line_flags           = 0;
// static uint8_t char_counter         = 0;
// static uint8_t comment_char_counter = 0;

// Spindle stop override control states.
struct SpindleStopBits {
    uint8_t enabled : 1;
    uint8_t initiate : 1;
    uint8_t restore : 1;
    uint8_t restoreCycle : 1;
};
union SpindleStop {
    uint8_t         value;
    SpindleStopBits bit;
};

static SpindleStop spindle_stop_ovr;

void protocol_reset() {
    probeState             = ProbeState::Off;
    soft_limit             = false;
    rtReset                = false;
    rtSafetyDoor           = false;
    spindle_stop_ovr.value = 0;

    // Do not clear rtAlarm because it might have been set during configuration
    // rtAlarm = ExecAlarm::None;
}

static int32_t idleEndTime = 0;

/*
  PRIMARY LOOP:
*/
Channel* exclusiveChannel = nullptr;

static void request_safety_door() {
    rtSafetyDoor = true;
}

void protocol_main_loop() {
    // Check for and report alarm state after a reset, error, or an initial power up.
    // NOTE: Sleep mode disables the stepper drivers and position can't be guaranteed.
    // Re-initialize the sleep state as an ALARM mode to ensure user homes or acknowledges.
    if (sys.state == State::ConfigAlarm) {
        report_feedback_message(Message::ConfigAlarmLock);
    } else {
        // Perform some machine checks to make sure everything is good to go.
        if (config->_start->_checkLimits && config->_axes->hasHardLimits()) {
            if (limits_get_state()) {
                sys.state = State::Alarm;  // Ensure alarm state is active.
                report_feedback_message(Message::CheckLimits);
            }
        }
        if (config->_control->startup_check()) {
            rtAlarm = ExecAlarm::ControlPin;
        }

        if (sys.state == State::Alarm || sys.state == State::Sleep) {
            report_feedback_message(Message::AlarmLock);
            sys.state = State::Alarm;  // Ensure alarm state is set.
        } else {
            // Check if the safety door is open.
            sys.state = State::Idle;
            if (config->_control->safety_door_ajar()) {
                request_safety_door();
                protocol_execute_realtime();  // Enter safety door mode. Should return as IDLE state.
            }
            // All systems go!
            settings_execute_startup();  // Execute startup script.
        }
    }

    // ---------------------------------------------------------------------------------
    // Primary loop! Upon a system abort, this exits back to main() to reset the system.
    // This is also where the system idles while waiting for something to do.
    // ---------------------------------------------------------------------------------
    for (;;) {
        // Poll the input sources waiting for a complete line to arrive
        while (true) {
            Channel* chan = nullptr;
            char     line[Channel::maxLine];
            protocol_execute_realtime();  // Runtime command check point.
            if (sys.abort) {
                return;  // Bail to calling function upon system abort
            }

            if (infile) {
                pollChannels();
                if (readyNext) {
                    readyNext = false;
                    chan      = &infile->getChannel();
                    switch (auto err = infile->readLine(line, Channel::maxLine)) {
                        case Error::Ok:
                            break;
                        case Error::Eof:
                            _notifyf("File job done", "%s file job succeeded", infile->path());
                            allChannels << "[MSG:" << infile->path() << " file job succeeded]\n";
                            delete infile;
                            infile = nullptr;
                            break;
                        default:
                            allChannels << "[MSG: ERR:" << static_cast<int>(err) << " (" << errorString(err) << ") in " << infile->path()
                                        << " at line " << infile->getLineNumber() << "]\n";
                            delete infile;
                            infile = nullptr;
                            break;
                    }
                }
            } else {
                chan = pollChannels(line);
            }
            if (chan == nullptr) {
                break;
            }
#ifdef DEBUG_REPORT_ECHO_RAW_LINE_RECEIVED
            report_echo_line_received(line, allChannels);
#endif
            display("GCODE", line);
            // auth_level can be upgraded by supplying a password on the command line
            report_status_message(execute_line(line, *chan, WebUI::AuthenticationLevel::LEVEL_GUEST), allChannels);
        }
        // If there are no more lines to be processed and executed,
        // auto-cycle start, if enabled, any queued moves.
        protocol_auto_cycle_start();
        protocol_execute_realtime();  // Runtime command check point.
        if (sys.abort) {
            return;  // Bail to main() program loop to reset system.
        }

        // check to see if we should disable the stepper drivers
        // If idleEndTime is 0, no disable is pending.

        // "(ticks() - EndTime) > 0" is a twos-complement arithmetic trick
        // for avoiding problems when the number space wraps around from
        // negative to positive or vice-versa.  It always works if EndTime
        // is set to "timer() + N" where N is less than half the number
        // space.  Using "timer() > EndTime" fails across the positive to
        // negative transition using signed comparison, and across the
        // negative to positive transition using unsigned.

        if (idleEndTime && (getCpuTicks() - idleEndTime) > 0) {
            idleEndTime = 0;  //
            config->_axes->set_disable(true);
        }
    }
    return; /* Never reached */
}

// Block until all buffered steps are executed or in a cycle state. Works with feed hold
// during a synchronize call, if it should happen. Also, waits for clean cycle end.
void protocol_buffer_synchronize() {
    do {
        // Restart motion if there are blocks in the planner queue
        protocol_auto_cycle_start();
        pollChannels();
        protocol_execute_realtime();  // Check and execute run-time commands
        if (sys.abort) {
            return;  // Check for system abort
        }
    } while (plan_get_current_block() || (sys.state == State::Cycle));
}

// Auto-cycle start triggers when there is a motion ready to execute and if the main program is not
// actively parsing commands.
// NOTE: This function is called from the main loop, buffer sync, and mc_move_motors() only and executes
// when one of these conditions exist respectively: There are no more blocks sent (i.e. streaming
// is finished, single commands), a command that needs to wait for the motions in the buffer to
// execute calls a buffer sync, or the planner buffer is full and ready to go.
void protocol_auto_cycle_start() {
    if (plan_get_current_block() != NULL && sys.state != State::Cycle &&
        sys.state != State::Hold) {             // Check if there are any blocks in the buffer.
        protocol_send_event(&cycleStartEvent);  // If so, execute them
    }
}

// This function is the general interface to the real-time command execution system. It is called
// from various check points in the main program, primarily where there may be a while loop waiting
// for a buffer to clear space or any point where the execution time from the last check point may
// be more than a fraction of a second. This is a way to execute realtime commands asynchronously
// (aka multitasking) with g-code parsing and planning functions. This function also serves
// as an interface for the interrupts to set the system realtime flags, where only the main program
// handles them, removing the need to define more computationally-expensive volatile variables. This
// also provides a controlled way to execute certain tasks without having two or more instances of
// the same task, such as the planner recalculating the buffer upon a feedhold or overrides.
// NOTE: The sys_rt_exec_state.bit variable flags are set by any process, step or serial interrupts, pinouts,
// limit switches, or the main program.
void protocol_execute_realtime() {
    protocol_exec_rt_system();
    if (sys.suspend.value) {
        protocol_exec_rt_suspend();
    }
}

static void alarm_msg(ExecAlarm alarm_code) {
    allChannels << "ALARM:" << static_cast<int>(alarm_code) << '\n';
    delay_ms(500);  // Force delay to ensure message clears serial write buffer.
}

// Executes run-time commands, when required. This function is the primary state
// machine that controls the various real-time features.
// NOTE: Do not alter this unless you know exactly what you are doing!
static void protocol_do_alarm() {
    if (spindle->_off_on_alarm) {
        spindle->stop();
    }

    if (rtAlarm == ExecAlarm::None) {
        return;
    }

    // System alarm. Everything has shutdown by something that has gone severely wrong. Report
    if (rtAlarm == ExecAlarm::HardLimit) {
        sys.state = State::Alarm;  // Set system alarm state
        alarm_msg(rtAlarm);
        report_feedback_message(Message::CriticalEvent);
        protocol_disable_steppers();
        rtReset = false;  // Disable any existing reset

        //WebUI::COMMANDS::restart_MCU();
        //WebUI::COMMANDS::handle();

        do {
            protocol_handle_events();
            // Block everything except reset and status reports until user issues reset or power
            // cycles. Hard limits typically occur while unattended or not paying attention. Gives
            // the user and a GUI time to do what is needed before resetting, like killing the
            // incoming stream. The same could be said about soft limits. While the position is not
            // lost, continued streaming could cause a serious crash if by chance it gets executed.
            pollChannels();  // Handle ^X realtime RESET command
        } while (!rtReset);
    }

    if (rtAlarm == ExecAlarm::SoftLimit) {
        sys.state = State::Alarm;  // Set system alarm state
        alarm_msg(rtAlarm);
    }

    rtAlarm = ExecAlarm::None;
}

static void protocol_start_holding() {
    if (!(sys.suspend.bit.motionCancel || sys.suspend.bit.jogCancel)) {  // Block, if already holding.
        sys.step_control = {};
        if (!Stepper::update_plan_block_parameters()) {  // Notify stepper module to recompute for hold deceleration.
            sys.step_control.endMotion = true;
        }
        sys.step_control.executeHold = true;  // Initiate suspend state with active flag.
    }
}

static void protocol_cancel_jogging() {
    if (!sys.suspend.bit.motionCancel) {
        sys.suspend.bit.jogCancel = true;
    }
}

static void protocol_hold_complete() {
    sys.suspend.value            = 0;
    sys.suspend.bit.holdComplete = true;
}

static void protocol_do_motion_cancel() {
    // log_debug("protocol_do_motion_cancel " << state_name());
    // Execute and flag a motion cancel with deceleration and return to idle. Used primarily by probing cycle
    // to halt and cancel the remainder of the motion.

    // MOTION_CANCEL only occurs during a CYCLE, but a HOLD and SAFETY_DOOR may have been initiated
    // beforehand. Motion cancel affects only a single planner block motion, while jog cancel
    // will handle and clear multiple planner block motions.
    switch (sys.state) {
        case State::Alarm:
        case State::ConfigAlarm:
        case State::CheckMode:
            return;  // Do not set motionCancel

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
            protocol_start_holding();
            break;

        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            // When jogging, we do not set motionCancel, hence return not break
            return;

        case State::Homing:
            // XXX maybe motion cancel should stop homing
        case State::Sleep:
        case State::Hold:
        case State::SafetyDoor:
            break;
    }
    sys.suspend.bit.motionCancel = true;
}

static void protocol_do_feedhold() {
    if (runLimitLoop) {
        runLimitLoop = false;  // Hack to stop show_limits()
        return;
    }
    // log_debug("protocol_do_feedhold " << state_name());
    // Execute a feed hold with deceleration, if required. Then, suspend system.
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::SafetyDoor:
        case State::Sleep:
            return;  // Do not change the state to Hold

        case State::Homing:
            // XXX maybe feedhold should stop homing
            log_info("Feedhold ignored while homing; use Reset instead");
            return;
        case State::Hold:
            break;

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
            protocol_start_holding();
            break;

        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            return;  // Do not change the state to Hold
    }
    sys.state = State::Hold;
}

static void protocol_do_safety_door() {
    // log_debug("protocol_do_safety_door " << int(sys.state));
    // Execute a safety door stop with a feed hold and disable spindle/coolant.
    // NOTE: Safety door differs from feed holds by stopping everything no matter state, disables powered
    // devices (spindle/coolant), and blocks resuming until switch is re-engaged.

    report_feedback_message(Message::SafetyDoorAjar);
    switch (sys.state) {
        case State::ConfigAlarm:
            return;
        case State::Alarm:
        case State::CheckMode:
        case State::Sleep:
            rtSafetyDoor = false;
            return;  // Do not change the state to SafetyDoor

        case State::Hold:
            break;
        case State::Homing:
            Machine::Homing::fail(ExecAlarm::HomingFailDoor);
            break;
        case State::SafetyDoor:
            if (!sys.suspend.bit.jogCancel && sys.suspend.bit.initiateRestore) {  // Actively restoring
                // Set hold and reset appropriate control flags to restart parking sequence.
                if (sys.step_control.executeSysMotion) {
                    Stepper::update_plan_block_parameters();  // Notify stepper module to recompute for hold deceleration.
                    sys.step_control                  = {};
                    sys.step_control.executeHold      = true;
                    sys.step_control.executeSysMotion = true;
                    sys.suspend.bit.holdComplete      = false;
                }  // else NO_MOTION is active.

                sys.suspend.bit.retractComplete = false;
                sys.suspend.bit.initiateRestore = false;
                sys.suspend.bit.restoreComplete = false;
                sys.suspend.bit.restartRetract  = true;
            }
            break;
        case State::Idle:
            protocol_hold_complete();
            break;
        case State::Cycle:
            protocol_start_holding();
            break;
        case State::Jog:
            protocol_start_holding();
            protocol_cancel_jogging();
            break;
    }
    if (!sys.suspend.bit.jogCancel) {
        // If jogging, leave the safety door event pending until the jog cancel completes
        rtSafetyDoor = false;
        sys.state    = State::SafetyDoor;
    }
    // NOTE: This flag doesn't change when the door closes, unlike sys.state. Ensures any parking motions
    // are executed if the door switch closes and the state returns to HOLD.
    sys.suspend.bit.safetyDoorAjar = true;
}

static void protocol_do_sleep() {
    // log_debug("protocol_do_sleep " << state_name());
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
            sys.suspend.bit.retractComplete = true;
            sys.suspend.bit.holdComplete    = true;
            break;

        case State::Idle:
            protocol_hold_complete();
            break;

        case State::Cycle:
        case State::Jog:
            protocol_start_holding();
            // Unlike other hold events, sleep does not set jogCancel
            break;

        case State::CheckMode:
        case State::Sleep:
        case State::Hold:
        case State::Homing:
        case State::SafetyDoor:
            break;
    }
    sys.state = State::Sleep;
}

void protocol_cancel_disable_steppers() {
    // Cancel any pending stepper disable.
    idleEndTime = 0;
}

static void protocol_do_initiate_cycle() {
    // log_debug("protocol_do_initiate_cycle " << state_name());
    // Start cycle only if queued motions exist in planner buffer and the motion is not canceled.
    sys.step_control = {};  // Restore step control to normal operation
    if (plan_get_current_block() && !sys.suspend.bit.motionCancel) {
        sys.suspend.value = 0;  // Break suspend state.
        sys.state         = State::Cycle;
        Stepper::prep_buffer();  // Initialize step segment buffer before beginning cycle.
        Stepper::wake_up();
    } else {  // Otherwise, do nothing. Set and resume IDLE state.

        sys.suspend.value = 0;  // Break suspend state.
        sys.state         = State::Idle;
    }
}
static void protocol_initiate_homing_cycle() {
    // log_debug("protocol_initiate_homing_cycle " << state_name());
    sys.step_control                  = {};    // Restore step control to normal operation
    sys.suspend.value                 = 0;     // Break suspend state.
    sys.step_control.executeSysMotion = true;  // Set to execute homing motion and clear existing flags.
    Stepper::prep_buffer();                    // Initialize step segment buffer before beginning cycle.
    Stepper::wake_up();
}

static void protocol_do_cycle_start() {
    // log_debug("protocol_do_cycle_start " << state_name());
    // Execute a cycle start by starting the stepper interrupt to begin executing the blocks in queue.

    // Resume door state when parking motion has retracted and door has been closed.
    switch (sys.state) {
        case State::SafetyDoor:
            if (!sys.suspend.bit.safetyDoorAjar) {
                if (sys.suspend.bit.restoreComplete) {
                    sys.state = State::Idle;
                    protocol_do_initiate_cycle();
                } else if (sys.suspend.bit.retractComplete) {
                    sys.suspend.bit.initiateRestore = true;
                }
            }
            break;
        case State::Idle:
            protocol_do_initiate_cycle();
            break;
        case State::Homing:
            protocol_initiate_homing_cycle();
            break;
        case State::Hold:
            // Cycle start only when IDLE or when a hold is complete and ready to resume.
            if (sys.suspend.bit.holdComplete) {
                if (spindle_stop_ovr.value) {
                    spindle_stop_ovr.bit.restoreCycle = true;  // Set to restore in suspend routine and cycle start after.
                } else {
                    protocol_do_initiate_cycle();
                }
            }
            break;
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Sleep:
        case State::Cycle:
        case State::Jog:
            break;
    }
}

void protocol_disable_steppers() {
    if (sys.state == State::Homing) {
        // Leave steppers enabled while homing
        config->_axes->set_disable(false);
        return;
    }
    if (sys.state == State::Sleep || rtAlarm != ExecAlarm::None) {
        // Disable steppers immediately in sleep or alarm state
        config->_axes->set_disable(true);
        return;
    }
    if (config->_stepping->_idleMsecs == 255) {
        // Leave steppers enabled if configured for "stay enabled"
        config->_axes->set_disable(false);
        return;
    }
    // Otherwise, schedule stepper disable in a few milliseconds
    // unless a disable time has already been scheduled
    if (idleEndTime == 0) {
        idleEndTime = usToEndTicks(config->_stepping->_idleMsecs * 1000);
        // idleEndTime 0 means that a stepper disable is not scheduled. so if we happen to
        // land on 0 as an end time, just push it back by one microsecond to get off 0.
        if (idleEndTime == 0) {
            idleEndTime = 1;
        }
    }
}

void protocol_do_cycle_stop() {
    // log_debug("protocol_do_cycle_stop " << state_name());
    protocol_disable_steppers();

    switch (sys.state) {
        case State::Hold:
        case State::SafetyDoor:
        case State::Sleep:
            // Reinitializes the cycle plan and stepper system after a feed hold for a resume. Called by
            // realtime command execution in the main program, ensuring that the planner re-plans safely.
            // NOTE: Bresenham algorithm variables are still maintained through both the planner and stepper
            // cycle reinitializations. The stepper path should continue exactly as if nothing has happened.
            // NOTE: cycleStopEvent is set by the stepper subsystem when a cycle or feed hold completes.
            if (!soft_limit && !sys.suspend.bit.jogCancel) {
                // Hold complete. Set to indicate ready to resume.  Remain in HOLD or DOOR states until user
                // has issued a resume command or reset.
                plan_cycle_reinitialize();
                if (sys.step_control.executeHold) {
                    sys.suspend.bit.holdComplete = true;
                }
                sys.step_control.executeHold      = false;
                sys.step_control.executeSysMotion = false;
                break;
            }
            // Fall through
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Idle:
        case State::Cycle:
        case State::Jog:
            // Motion complete. Includes CYCLE/JOG/HOMING states and jog cancel/motion cancel/soft limit events.
            // NOTE: Motion and jog cancel both immediately return to idle after the hold completes.
            if (sys.suspend.bit.jogCancel) {  // For jog cancel, flush buffers and sync positions.
                sys.step_control = {};
                plan_reset();
                Stepper::reset();
                gc_sync_position();
                plan_sync_position();
            }
            if (sys.suspend.bit.safetyDoorAjar) {  // Only occurs when safety door opens during jog.
                sys.suspend.bit.jogCancel    = false;
                sys.suspend.bit.holdComplete = true;
                sys.state                    = State::SafetyDoor;
            } else {
                sys.suspend.value = 0;
                sys.state         = State::Idle;
            }
            break;
        case State::Homing:
            Machine::Homing::cycleStop();
            break;
    }
}

static void update_velocities() {
    report_ovr_counter = 0;  // Set to report change immediately
    plan_update_velocity_profile_parameters();
    plan_cycle_reinitialize();
}

// This is the final phase of the shutdown activity that is initiated by mc_reset().
// The stuff herein is not necessarily safe to do in an ISR.
static void protocol_do_late_reset() {
    // Kill spindle and coolant.
    spindle->stop();
    report_ovr_counter = 0;  // Set to report change immediately
    config->_coolant->stop();

    protocol_disable_steppers();
    config->_stepping->reset();

    // turn off all User I/O immediately
    config->_userOutputs->all_off();

    // do we need to stop a running file job?
    if (infile) {
        //Report print stopped
        _notifyf("File print canceled", "Reset during file job at line: %d", infile->getLineNumber());
        // log_info() does not work well in this case because the message gets broken in half
        // by report_init_message().  The flow of control that causes it is obscure.
        allChannels << "[MSG:"
                    << "Reset during file job at line: " << infile->getLineNumber();
        delete infile;
        infile = nullptr;
    }
}

void protocol_exec_rt_system() {
    protocol_do_alarm();  // If there is a hard or soft limit, this will block until rtReset is set

    if (rtReset) {
        if (sys.state == State::Homing) {
            Machine::Homing::fail(ExecAlarm::HomingFailReset);
        }
        protocol_do_late_reset();
        // Trigger system abort.
        sys.abort = true;  // Only place this is set true.
        return;            // Nothing else to do but exit.
    }

    if (rtSafetyDoor) {
        protocol_do_safety_door();
    }

    protocol_handle_events();

    // Reload step segment buffer
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::Idle:
        case State::Sleep:
            break;
        case State::Cycle:
        case State::Hold:
        case State::SafetyDoor:
        case State::Homing:
        case State::Jog:
            Stepper::prep_buffer();
            break;
    }
}

static void protocol_manage_spindle() {
    // Feed hold manager. Controls spindle stop override states.
    // NOTE: Hold ensured as completed by condition check at the beginning of suspend routine.
    if (spindle_stop_ovr.value) {
        // Handles beginning of spindle stop
        if (spindle_stop_ovr.bit.initiate) {
            if (gc_state.modal.spindle != SpindleState::Disable) {
                spindle->spinDown();
                report_ovr_counter           = 0;  // Set to report change immediately
                spindle_stop_ovr.value       = 0;
                spindle_stop_ovr.bit.enabled = true;  // Set stop override state to enabled, if de-energized.
            } else {
                spindle_stop_ovr.value = 0;  // Clear stop override state
            }
            // Handles restoring of spindle state
        } else if (spindle_stop_ovr.bit.restore || spindle_stop_ovr.bit.restoreCycle) {
            if (gc_state.modal.spindle != SpindleState::Disable) {
                report_feedback_message(Message::SpindleRestore);
                if (spindle->isRateAdjusted()) {
                    // When in laser mode, defer turn on until cycle starts
                    sys.step_control.updateSpindleSpeed = true;
                } else {
                    config->_parking->restore_spindle();
                    report_ovr_counter = 0;  // Set to report change immediately
                }
            }
            if (spindle_stop_ovr.bit.restoreCycle) {
                protocol_send_event(&cycleStartEvent);  // Resume program.
            }
            spindle_stop_ovr.value = 0;  // Clear stop override state
        }
    } else {
        // Handles spindle state during hold. NOTE: Spindle speed overrides may be altered during hold state.
        // NOTE: sys.step_control.updateSpindleSpeed is automatically reset upon resume in step generator.
        if (sys.step_control.updateSpindleSpeed) {
            config->_parking->restore_spindle();
            sys.step_control.updateSpindleSpeed = false;
        }
    }
}

// Handles system suspend procedures, such as feed hold, safety door, and parking motion.
// The system will enter this loop, create local variables for suspend tasks, and return to
// whatever function that invoked the suspend, resuming normal operation.
static void protocol_exec_rt_suspend() {
    config->_parking->setup();

    if (spindle->isRateAdjusted()) {
        protocol_send_event(&accessoryOverrideEvent, (void*)AccessoryOverride::SpindleStopOvr);
    }

    while (sys.suspend.value) {
        if (sys.abort) {
            return;
        }
        // if a jogCancel comes in and we have a jog "in-flight" (parsed and handed over to mc_move_motors()),
        //  then we need to cancel it before it reaches the planner.  otherwise we may try to move way out of
        //  normal bounds, especially with senders that issue a series of jog commands before sending a cancel.
        if (sys.suspend.bit.jogCancel) {
            mc_cancel_jog();
        }
        // Block until initial hold is complete and the machine has stopped motion.
        if (sys.suspend.bit.holdComplete) {
            // Parking manager. Handles de/re-energizing, switch state checks, and parking motions for
            // the safety door and sleep states.
            if (sys.state == State::SafetyDoor || sys.state == State::Sleep) {
                // Handles retraction motions and de-energizing.
                config->_parking->set_target();
                if (!sys.suspend.bit.retractComplete) {
                    // Ensure any prior spindle stop override is disabled at start of safety door routine.
                    spindle_stop_ovr.value = 0;  // Disable override

                    // Execute slow pull-out parking retract motion. Parking requires homing enabled, the
                    // current location not exceeding the parking target location, and laser mode disabled.
                    // NOTE: State will remain DOOR, until the de-energizing and retract is complete.
                    config->_parking->park(sys.suspend.bit.restartRetract);

                    sys.suspend.bit.retractComplete = true;
                    sys.suspend.bit.restartRetract  = false;
                } else {
                    if (sys.state == State::Sleep) {
                        report_feedback_message(Message::SleepMode);
                        // Spindle and coolant should already be stopped, but do it again just to be sure.
                        spindle->spinDown();
                        config->_coolant->off();
                        report_ovr_counter = 0;  // Set to report change immediately
                        Stepper::go_idle();      // Stop stepping and maybe disable steppers
                        while (!(sys.abort)) {
                            protocol_exec_rt_system();  // Do nothing until reset.
                        }
                        return;  // Abort received. Return to re-initialize.
                    }
                    // Allows resuming from parking/safety door. Polls to see if safety door is closed and ready to resume.
                    if (sys.state == State::SafetyDoor && !config->_control->safety_door_ajar()) {
                        if (sys.suspend.bit.safetyDoorAjar) {
                            log_info("Safety door closed.  Issue cycle start to resume");
                        }
                        sys.suspend.bit.safetyDoorAjar = false;  // Reset door ajar flag to denote ready to resume.
                    }
                    if (sys.suspend.bit.initiateRestore) {
                        config->_parking->unpark(sys.suspend.bit.restartRetract);

                        if (!sys.suspend.bit.restartRetract && sys.state == State::SafetyDoor && !sys.suspend.bit.safetyDoorAjar) {
                            sys.state = State::Idle;
                            protocol_send_event(&cycleStartEvent);  // Resume program.
                        }
                    }
                }
            } else {
                protocol_manage_spindle();
            }
        }
        pollChannels();  // Handle realtime commands like status report, cycle start and reset
        protocol_exec_rt_system();
    }
}

static void protocol_do_feed_override(void* incrementvp) {
    int increment = int(incrementvp);
    int percent;
    if (increment == FeedOverride::Default) {
        percent = FeedOverride::Default;
    } else {
        percent = sys.f_override + increment;
        if (percent > FeedOverride::Max) {
            percent = FeedOverride::Max;
        } else if (percent < FeedOverride::Min) {
            percent = FeedOverride::Min;
        }
    }
    if (percent != sys.f_override) {
        sys.f_override = percent;
        update_velocities();
    }
}

static void protocol_do_rapid_override(void* percentvp) {
    int percent = int(percentvp);
    if (percent != sys.r_override) {
        sys.r_override = percent;
        update_velocities();
    }
}

static void protocol_do_spindle_override(void* incrementvp) {
    int percent;
    int increment = int(incrementvp);
    if (increment == SpindleSpeedOverride::Default) {
        percent = SpindleSpeedOverride::Default;
    } else {
        percent = sys.spindle_speed_ovr + increment;
        if (percent > SpindleSpeedOverride::Max) {
            percent = SpindleSpeedOverride::Max;
        } else if (percent < SpindleSpeedOverride::Min) {
            percent = SpindleSpeedOverride::Min;
        }
    }
    if (percent != sys.spindle_speed_ovr) {
        sys.spindle_speed_ovr               = percent;
        sys.step_control.updateSpindleSpeed = true;
        report_ovr_counter                  = 0;  // Set to report change immediately

        // If spindle is on, tell it the RPM has been overridden
        // When moving, the override is handled by the stepping code
        if (gc_state.modal.spindle != SpindleState::Disable && !inMotionState()) {
            spindle->setState(gc_state.modal.spindle, gc_state.spindle_speed);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }
}

static void protocol_do_accessory_override(void* type) {
    switch (int(type)) {
        case AccessoryOverride::SpindleStopOvr:
            // Spindle stop override allowed only while in HOLD state.
            if (sys.state == State::Hold) {
                if (spindle_stop_ovr.value == 0) {
                    spindle_stop_ovr.bit.initiate = true;
                } else if (spindle_stop_ovr.bit.enabled) {
                    spindle_stop_ovr.bit.restore = true;
                }
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        case AccessoryOverride::FloodToggle:
            // NOTE: Since coolant state always performs a planner sync whenever it changes, the current
            // run state can be determined by checking the parser state.
            if (config->_coolant->hasFlood() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
                gc_state.modal.coolant.Flood = !gc_state.modal.coolant.Flood;
                config->_coolant->set_state(gc_state.modal.coolant);
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        case AccessoryOverride::MistToggle:
            if (config->_coolant->hasMist() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
                gc_state.modal.coolant.Mist = !gc_state.modal.coolant.Mist;
                config->_coolant->set_state(gc_state.modal.coolant);
                report_ovr_counter = 0;  // Set to report change immediately
            }
            break;
        default:
            break;
    }
}

static void protocol_do_limit(void* arg) {
    Machine::LimitPin* limit = (Machine::LimitPin*)arg;
    if (sys.state == State::Homing) {
        Machine::Homing::limitReached();
        return;
    }
    log_debug("Limit switch tripped for " << config->_axes->axisName(limit->_axis) << " motor " << limit->_motorNum);
    if (sys.state == State::Cycle || sys.state == State::Jog) {
        if (limit->isHard() && rtAlarm == ExecAlarm::None) {
            log_debug("Hard limits");
            mc_reset();                      // Initiate system kill.
            rtAlarm = ExecAlarm::HardLimit;  // Indicate hard limit critical event
        }
        return;
    }
}
ArgEvent feedOverrideEvent { protocol_do_feed_override };
ArgEvent rapidOverrideEvent { protocol_do_rapid_override };
ArgEvent spindleOverrideEvent { protocol_do_spindle_override };
ArgEvent accessoryOverrideEvent { protocol_do_accessory_override };
ArgEvent limitEvent { protocol_do_limit };

ArgEvent reportStatusEvent { (void (*)(void*))report_realtime_status };

NoArgEvent safetyDoorEvent { request_safety_door };
NoArgEvent feedHoldEvent { protocol_do_feedhold };
NoArgEvent cycleStartEvent { protocol_do_cycle_start };
NoArgEvent cycleStopEvent { protocol_do_cycle_stop };
NoArgEvent motionCancelEvent { protocol_do_motion_cancel };
NoArgEvent sleepEvent { protocol_do_sleep };
NoArgEvent debugEvent { report_realtime_debug };

// Only mc_reset() is permitted to set rtReset.
NoArgEvent resetEvent { mc_reset };

// The problem is that report_realtime_status needs a channel argument
// Event statusReportEvent { protocol_do_status_report(XXX) };

xQueueHandle event_queue;

void protocol_init() {
    event_queue = xQueueCreate(10, sizeof(EventItem));
}

void IRAM_ATTR protocol_send_event_from_ISR(Event* evt, void* arg) {
    EventItem item { evt, arg };
    xQueueSendFromISR(event_queue, &item, NULL);
}

void protocol_send_event(Event* evt, void* arg) {
    EventItem item { evt, arg };
    xQueueSend(event_queue, &item, 0);
}
void protocol_handle_events() {
    EventItem item;
    while (xQueueReceive(event_queue, &item, 0)) {
        // log_debug("event");
        item.event->run(item.arg);
    }
}
