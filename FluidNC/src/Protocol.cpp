// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Protocol.cpp - execution state machine
*/

#include "Protocol.h"

#include "Machine/MachineConfig.h"
#include "Report.h"         // report_feedback_message
#include "Limits.h"         // limits_get_state, soft_limit
#include "Planner.h"        // plan_get_current_block
#include "MotionControl.h"  // PARKING_MOTION_LINE_NUMBER
#include "Settings.h"       // settings_execute_startup
#include "InputFile.h"      // infile

#ifdef DEBUG_STEPPING
volatile bool rtCrash;
volatile bool rtSeq;
volatile bool rtSegSeq;
volatile bool rtTestPl;
volatile bool rtTestSt;
uint32_t      expected_steps[MAX_N_AXIS];
#endif

#ifdef DEBUG_REPORT_REALTIME
volatile bool rtExecDebug;
#endif

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

volatile Accessory rtAccessoryOverride;  // Global realtime executor bitflag variable for spindle/coolant overrides.
volatile Percent   rtFOverride;          // Global realtime executor feedrate override percentage
volatile Percent   rtROverride;          // Global realtime executor rapid override percentage
volatile Percent   rtSOverride;          // Global realtime executor spindle override percentage

volatile bool rtStatusReport;
volatile bool rtCycleStart;
volatile bool rtFeedHold;
volatile bool rtReset;
volatile bool rtSafetyDoor;
volatile bool rtMotionCancel;
volatile bool rtSleep;
volatile bool rtCycleStop;  // For state transitions, instead of bitflag
volatile bool rtButtonMacro0;
volatile bool rtButtonMacro1;
volatile bool rtButtonMacro2;
volatile bool rtButtonMacro3;

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

bool can_park() {
    if (spindle->isRateAdjusted()) {
        return false;
    }
    if (config->_enableParkingOverrideControl) {
        return sys.override_ctrl == Override::ParkingMotion && Machine::Axes::homingMask;
    } else {
        return Machine::Axes::homingMask;
    }
}

void protocol_reset() {
    probeState                = ProbeState::Off;
    rtStatusReport            = false;
    rtCycleStart              = false;
    rtFeedHold                = false;
    rtReset                   = false;
    rtSafetyDoor              = false;
    rtMotionCancel            = false;
    rtSleep                   = false;
    rtCycleStop               = false;
    rtAccessoryOverride.value = 0;
    rtFOverride               = FeedOverride::Default;
    rtROverride               = RapidOverride::Default;
    rtSOverride               = SpindleSpeedOverride::Default;
    spindle_stop_ovr.value    = 0;

    // Do not clear rtAlarm because it might have been set during configuration
    // rtAlarm = ExecAlarm::None;
}

static int32_t idleEndTime = 0;

/*
  PRIMARY LOOP:
*/
Channel* exclusiveChannel = nullptr;

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

        if (sys.state == State::Alarm || sys.state == State::Sleep) {
            report_feedback_message(Message::AlarmLock);
            sys.state = State::Alarm;  // Ensure alarm state is set.
        } else {
            // Check if the safety door is open.
            sys.state = State::Idle;
            if (config->_control->system_check_safety_door_ajar()) {
                rtSafetyDoor = true;
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
                            *chan << "[MSG:" << infile->path() << " file job succeeded]\n";
                            delete infile;
                            infile = nullptr;
                            break;
                        default:
                            *chan << "[MSG: ERR:" << static_cast<int>(err) << " (" << errorString(err) << ") in " << infile->path()
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
            report_echo_line_received(line, *chan);
#endif
            display("GCODE", line);
            // auth_level can be upgraded by supplying a password on the command line
            report_status_message(execute_line(line, *chan, WebUI::AuthenticationLevel::LEVEL_GUEST), *chan);
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
    // If system is queued, ensure cycle resumes if the auto start flag is present.
    protocol_auto_cycle_start();
    do {
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
    if (plan_get_current_block() != NULL) {  // Check if there are any blocks in the buffer.
        rtCycleStart = true;                 // If so, execute them!
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
    switch (rtAlarm) {
        case ExecAlarm::None:
            return;
        // System alarm. Everything has shutdown by something that has gone severely wrong. Report
        case ExecAlarm::HardLimit:
        case ExecAlarm::SoftLimit:
            sys.state = State::Alarm;  // Set system alarm state
            alarm_msg(rtAlarm);
            report_feedback_message(Message::CriticalEvent);
            rtReset = false;  // Disable any existing reset
            do {
                // Block everything except reset and status reports until user issues reset or power
                // cycles. Hard limits typically occur while unattended or not paying attention. Gives
                // the user and a GUI time to do what is needed before resetting, like killing the
                // incoming stream. The same could be said about soft limits. While the position is not
                // lost, continued streaming could cause a serious crash if by chance it gets executed.
                pollChannels();  // Handle ^X realtime RESET command
            } while (!rtReset);
            break;
        default:
            sys.state = State::Alarm;  // Set system alarm state
            alarm_msg(rtAlarm);
            break;
    }
    rtAlarm = ExecAlarm::None;
}

static void protocol_start_holding() {
    if (!(sys.suspend.bit.motionCancel || sys.suspend.bit.jogCancel)) {  // Block, if already holding.
        Stepper::update_plan_block_parameters();                         // Notify stepper module to recompute for hold deceleration.
        sys.step_control             = {};
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
    // Execute and flag a motion cancel with deceleration and return to idle. Used primarily by probing cycle
    // to halt and cancel the remainder of the motion.
    rtMotionCancel = false;
    rtCycleStart   = false;  // Cancel any pending start

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

        case State::Sleep:
        case State::Hold:
        case State::Homing:
        case State::SafetyDoor:
            break;
    }
    sys.suspend.bit.motionCancel = true;
}

static void protocol_do_feedhold() {
    // Execute a feed hold with deceleration, if required. Then, suspend system.
    rtFeedHold   = false;
    rtCycleStart = false;  // Cancel any pending start
    switch (sys.state) {
        case State::ConfigAlarm:
        case State::Alarm:
        case State::CheckMode:
        case State::SafetyDoor:
        case State::Sleep:
            return;  // Do not change the state to Hold

        case State::Hold:
        case State::Homing:
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
    // Execute a safety door stop with a feed hold and disable spindle/coolant.
    // NOTE: Safety door differs from feed holds by stopping everything no matter state, disables powered
    // devices (spindle/coolant), and blocks resuming until switch is re-engaged.

    rtCycleStart = false;  // Cancel any pending start
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
            rtAlarm = ExecAlarm::HomingFailDoor;
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
    rtSleep = false;
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
    // Start cycle only if queued motions exist in planner buffer and the motion is not canceled.
    sys.step_control = {};  // Restore step control to normal operation
    if (plan_get_current_block() && !sys.suspend.bit.motionCancel) {
        sys.suspend.value = 0;  // Break suspend state.
        sys.state         = State::Cycle;
        Stepper::prep_buffer();  // Initialize step segment buffer before beginning cycle.
        Stepper::wake_up();
        // Make sure the steppers can't be scheduled for a shutdown while this cycle is running.
        protocol_cancel_disable_steppers();
    } else {                    // Otherwise, do nothing. Set and resume IDLE state.
        sys.suspend.value = 0;  // Break suspend state.
        sys.state         = State::Idle;
    }
}

// The handlers for rtFeedHold, rtMotionCancel, and rtsDafetyDoor clear rtCycleStart to
// ensure that auto-cycle-start does not resume a hold without explicit user input.
static void protocol_do_cycle_start() {
    rtCycleStart = false;

    // Execute a cycle start by starting the stepper interrupt to begin executing the blocks in queue.

    // Resume door state when parking motion has retracted and door has been closed.
    switch (sys.state) {
        case State::SafetyDoor:
            if (!sys.suspend.bit.safetyDoorAjar) {
                if (sys.suspend.bit.restoreComplete) {
                    sys.state = State::Idle;  // Set to IDLE to immediately resume the cycle.
                } else if (sys.suspend.bit.retractComplete) {
                    // Flag to re-energize powered components and restore original position, if disabled by SAFETY_DOOR.
                    // NOTE: For a safety door to resume, the switch must be closed, as indicated by HOLD state, and
                    // the retraction execution is complete, which implies the initial feed hold is not active. To
                    // restore normal operation, the restore procedures must be initiated by the following flag. Once,
                    // they are complete, it will call CYCLE_START automatically to resume and exit the suspend.
                    sys.suspend.bit.initiateRestore = true;
                }
            }
            break;
        case State::Idle:
            protocol_do_initiate_cycle();
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
        case State::Homing:
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
    rtCycleStop = false;

    protocol_disable_steppers();

    switch (sys.state) {
        case State::Hold:
        case State::SafetyDoor:
        case State::Sleep:
            // Reinitializes the cycle plan and stepper system after a feed hold for a resume. Called by
            // realtime command execution in the main program, ensuring that the planner re-plans safely.
            // NOTE: Bresenham algorithm variables are still maintained through both the planner and stepper
            // cycle reinitializations. The stepper path should continue exactly as if nothing has happened.
            // NOTE: rtCycleStop is set by the stepper subsystem when a cycle or feed hold completes.
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
        case State::Homing:
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
    }
}

static void protocol_execute_overrides() {
    // Execute overrides.
    if ((rtFOverride != sys.f_override) || (rtROverride != sys.r_override)) {
        sys.f_override     = rtFOverride;
        sys.r_override     = rtROverride;
        report_ovr_counter = 0;  // Set to report change immediately
        plan_update_velocity_profile_parameters();
        plan_cycle_reinitialize();
    }

    // NOTE: Unlike motion overrides, spindle overrides do not require a planner reinitialization.
    if (rtSOverride != sys.spindle_speed_ovr) {
        sys.step_control.updateSpindleSpeed = true;
        sys.spindle_speed_ovr               = rtSOverride;
        report_ovr_counter                  = 0;  // Set to report change immediately

        // XXX this might not be necessary if the override is processed at the right level
        // If spindle is on, tell it the RPM has been overridden
        if (gc_state.modal.spindle != SpindleState::Disable) {
            spindle->setState(gc_state.modal.spindle, gc_state.spindle_speed);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }

    if (rtAccessoryOverride.bit.spindleOvrStop) {
        rtAccessoryOverride.bit.spindleOvrStop = false;
        // Spindle stop override allowed only while in HOLD state.
        if (sys.state == State::Hold) {
            if (spindle_stop_ovr.value == 0) {
                spindle_stop_ovr.bit.initiate = true;
            } else if (spindle_stop_ovr.bit.enabled) {
                spindle_stop_ovr.bit.restore = true;
            }
        }
    }

    // NOTE: Since coolant state always performs a planner sync whenever it changes, the current
    // run state can be determined by checking the parser state.
    if (rtAccessoryOverride.bit.coolantFloodOvrToggle) {
        rtAccessoryOverride.bit.coolantFloodOvrToggle = false;
        if (config->_coolant->hasFlood() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
            gc_state.modal.coolant.Flood = !gc_state.modal.coolant.Flood;
            config->_coolant->set_state(gc_state.modal.coolant);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }
    if (rtAccessoryOverride.bit.coolantMistOvrToggle) {
        rtAccessoryOverride.bit.coolantMistOvrToggle = false;

        if (config->_coolant->hasMist() && (sys.state == State::Idle || sys.state == State::Cycle || sys.state == State::Hold)) {
            gc_state.modal.coolant.Mist = !gc_state.modal.coolant.Mist;
            config->_coolant->set_state(gc_state.modal.coolant);
            report_ovr_counter = 0;  // Set to report change immediately
        }
    }
}

// This is the final phase of the shutdown activity that is initiated by mc_reset().
// The stuff herein is not necessarily safe to do in an ISR.
static void protocol_do_late_reset() {
    // Kill spindle and coolant.
    spindle->stop();
    report_ovr_counter = 0;  // Set to report change immediately
    config->_coolant->stop();

    // turn off all User I/O immediately
    config->_userOutputs->all_off();

    // do we need to stop a running file job?
    if (infile) {
        //Report print stopped
        _notifyf("File print canceled", "Reset during file job at line: %d", infile->getLineNumber());
        // log_info() does not work well in this case because the message gets broken in half
        // by report_init_message().  The flow of control that causes it is obscure.
        infile->getChannel() << "[MSG:"
                             << "Reset during file job at line: " << infile->getLineNumber();
        delete infile;
        infile = nullptr;
    }
}

void protocol_do_macro(int macro_num) {
    // must be in Idle
    if (sys.state != State::Idle) {
        log_error("Macro button only permitted in idle");
        return;
    }

    config->_macros->run_macro(macro_num);
}

void protocol_exec_rt_system() {
    protocol_do_alarm();  // If there is a hard or soft limit, this will block until rtReset is set

#ifdef DEBUG_STEPPING
    if (rtSegSeq) {
        rtSegSeq = false;
        log_error("segment exp " << seg_seq_exp << " actual " << seg_seq_act);
        rtReset = true;
    }
    if (rtSeq) {
        rtSeq = false;
        log_error("planner " << pl_seq0 << " stepper " << st_seq0);
        rtReset = true;
    }
    if (rtCrash) {
        rtCrash = false;
        log_error("Stepper exp,actual " << expected_steps[0] << "," << motor_steps[0] << " " << expected_steps[1] << "," << motor_steps[1]
                                        << " " << expected_steps[2] << "," << motor_steps[2]);
        rtReset = true;
    }
    if (rtTestPl) {
        rtTestPl = false;
        log_info("Poisoned planner_seq");
        planner_seq += 20;
    }
    if (rtTestSt) {
        rtTestSt = false;
        log_info("Poisoned stepper");
        --motor_steps[0];
    }
#endif
    if (rtReset) {
        if (sys.state == State::Homing) {
            rtAlarm = ExecAlarm::HomingFailReset;
        }
        protocol_do_late_reset();
        // Trigger system abort.
        sys.abort = true;  // Only place this is set true.
        return;            // Nothing else to do but exit.
    }

    if (rtStatusReport) {
        rtStatusReport = false;
        report_realtime_status(allChannels);
    }

    if (rtMotionCancel) {
        protocol_do_motion_cancel();
    }

    if (rtFeedHold) {
        protocol_do_feedhold();
    }

    if (rtSafetyDoor) {
        protocol_do_safety_door();
    }

    if (rtSleep) {
        protocol_do_sleep();
    }

    if (rtCycleStart) {
        protocol_do_cycle_start();
    }

    if (rtCycleStop) {
        protocol_do_cycle_stop();
    }

    if (rtButtonMacro0) {
        rtButtonMacro0 = false;
        protocol_do_macro(0);
    }
    if (rtButtonMacro1) {
        rtButtonMacro1 = false;
        protocol_do_macro(1);
    }
    if (rtButtonMacro2) {
        rtButtonMacro2 = false;
        protocol_do_macro(2);
    }
    if (rtButtonMacro3) {
        rtButtonMacro3 = false;
        protocol_do_macro(3);
    }

    protocol_execute_overrides();

#ifdef DEBUG_PROTOCOL
    if (rtExecDebug) {
        rtExecDebug = false;
        report_realtime_debug();
    }
#endif
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

// Handles system suspend procedures, such as feed hold, safety door, and parking motion.
// The system will enter this loop, create local variables for suspend tasks, and return to
// whatever function that invoked the suspend, resuming normal operation.
// This function is written in a way to promote custom parking motions. Simply use this as a
// template
static void protocol_exec_rt_suspend() {
    // Declare and initialize parking local variables
    float             restore_target[MAX_N_AXIS];
    float             retract_waypoint = PARKING_PULLOUT_INCREMENT;
    plan_line_data_t  plan_data;
    plan_line_data_t* pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t));
    pl_data->motion                = {};
    pl_data->motion.systemMotion   = 1;
    pl_data->motion.noFeedOverride = 1;
    pl_data->line_number           = PARKING_MOTION_LINE_NUMBER;

    plan_block_t* block = plan_get_current_block();
    CoolantState  restore_coolant;
    SpindleState  restore_spindle;
    SpindleSpeed  restore_spindle_speed;
    if (block == NULL) {
        restore_coolant       = gc_state.modal.coolant;
        restore_spindle       = gc_state.modal.spindle;
        restore_spindle_speed = gc_state.spindle_speed;
    } else {
        restore_coolant       = block->coolant;
        restore_spindle       = block->spindle;
        restore_spindle_speed = block->spindle_speed;
    }
    if (spindle->isRateAdjusted()) {
        rtAccessoryOverride.bit.spindleOvrStop = true;
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
                float* parking_target = get_mpos();
                if (!sys.suspend.bit.retractComplete) {
                    // Ensure any prior spindle stop override is disabled at start of safety door routine.
                    spindle_stop_ovr.value = 0;  // Disable override

                    // Get current position and store restore location and spindle retract waypoint.
                    if (!sys.suspend.bit.restartRetract) {
                        memcpy(restore_target, parking_target, sizeof(restore_target[0]) * config->_axes->_numberAxis);
                        retract_waypoint += restore_target[PARKING_AXIS];
                        retract_waypoint = MIN(retract_waypoint, PARKING_TARGET);
                    }
                    // Execute slow pull-out parking retract motion. Parking requires homing enabled, the
                    // current location not exceeding the parking target location, and laser mode disabled.
                    // NOTE: State will remain DOOR, until the de-energizing and retract is complete.
                    if (can_park() && parking_target[PARKING_AXIS] < PARKING_TARGET) {
                        // Retract spindle by pullout distance. Ensure retraction motion moves away from
                        // the workpiece and waypoint motion doesn't exceed the parking target location.
                        if (parking_target[PARKING_AXIS] < retract_waypoint) {
                            parking_target[PARKING_AXIS] = retract_waypoint;
                            pl_data->feed_rate           = PARKING_PULLOUT_RATE;
                            pl_data->coolant             = restore_coolant;
                            pl_data->spindle             = restore_spindle;
                            pl_data->spindle_speed       = restore_spindle_speed;
                            mc_parking_motion(parking_target, pl_data);
                        }
                        // NOTE: Clear accessory state after retract and after an aborted restore motion.
                        pl_data->spindle               = SpindleState::Disable;
                        pl_data->coolant               = {};
                        pl_data->motion                = {};
                        pl_data->motion.systemMotion   = 1;
                        pl_data->motion.noFeedOverride = 1;
                        pl_data->spindle_speed         = 0.0;
                        spindle->spinDown();
                        report_ovr_counter = 0;  // Set to report change immediately
                        // Execute fast parking retract motion to parking target location.
                        if (parking_target[PARKING_AXIS] < PARKING_TARGET) {
                            parking_target[PARKING_AXIS] = PARKING_TARGET;
                            pl_data->feed_rate           = PARKING_RATE;
                            mc_parking_motion(parking_target, pl_data);
                        }
                    } else {
                        // Parking motion not possible. Just disable the spindle and coolant.
                        // NOTE: Laser mode does not start a parking motion to ensure the laser stops immediately.
                        spindle->spinDown();
                        config->_coolant->off();
                        report_ovr_counter = 0;  // Set to report changes immediately
                    }

                    sys.suspend.bit.restartRetract  = false;
                    sys.suspend.bit.retractComplete = true;
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
                    // Allows resuming from parking/safety door. Actively checks if safety door is closed and ready to resume.
                    if (sys.state == State::SafetyDoor) {
                        if (!config->_control->system_check_safety_door_ajar()) {
                            sys.suspend.bit.safetyDoorAjar = false;  // Reset door ajar flag to denote ready to resume.
                        }
                    }
                    // Handles parking restore and safety door resume.
                    if (sys.suspend.bit.initiateRestore) {
                        // Execute fast restore motion to the pull-out position. Parking requires homing enabled.
                        // NOTE: State is will remain DOOR, until the de-energizing and retract is complete.
                        if (can_park()) {
                            // Check to ensure the motion doesn't move below pull-out position.
                            if (parking_target[PARKING_AXIS] <= PARKING_TARGET) {
                                parking_target[PARKING_AXIS] = retract_waypoint;
                                pl_data->feed_rate           = PARKING_RATE;
                                mc_parking_motion(parking_target, pl_data);
                            }
                        }
                        // Delayed Tasks: Restart spindle and coolant, delay to power-up, then resume cycle.
                        if (gc_state.modal.spindle != SpindleState::Disable) {
                            // Block if safety door re-opened during prior restore actions.
                            if (!sys.suspend.bit.restartRetract) {
                                if (spindle->isRateAdjusted()) {
                                    // When in laser mode, defer turn on until cycle starts
                                    sys.step_control.updateSpindleSpeed = true;
                                } else {
                                    spindle->setState(restore_spindle, restore_spindle_speed);
                                    report_ovr_counter = 0;  // Set to report change immediately
                                }
                            }
                        }
                        if (gc_state.modal.coolant.Flood || gc_state.modal.coolant.Mist) {
                            // Block if safety door re-opened during prior restore actions.
                            if (!sys.suspend.bit.restartRetract) {
                                config->_coolant->set_state(restore_coolant);
                                report_ovr_counter = 0;  // Set to report change immediately
                            }
                        }

                        // Execute slow plunge motion from pull-out position to resume position.
                        if (can_park()) {
                            // Block if safety door re-opened during prior restore actions.
                            if (!sys.suspend.bit.restartRetract) {
                                // Regardless if the retract parking motion was a valid/safe motion or not, the
                                // restore parking motion should logically be valid, either by returning to the
                                // original position through valid machine space or by not moving at all.
                                pl_data->feed_rate     = PARKING_PULLOUT_RATE;
                                pl_data->spindle       = restore_spindle;
                                pl_data->coolant       = restore_coolant;
                                pl_data->spindle_speed = restore_spindle_speed;
                                mc_parking_motion(restore_target, pl_data);
                            }
                        }
                        if (!sys.suspend.bit.restartRetract) {
                            sys.suspend.bit.restoreComplete = true;
                            rtCycleStart                    = true;  // Set to resume program.
                        }
                    }
                }
            } else {
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
                                spindle->setState(restore_spindle, restore_spindle_speed);
                                report_ovr_counter = 0;  // Set to report change immediately
                            }
                        }
                        if (spindle_stop_ovr.bit.restoreCycle) {
                            rtCycleStart = true;  // Set to resume program.
                        }
                        spindle_stop_ovr.value = 0;  // Clear stop override state
                    }
                } else {
                    // Handles spindle state during hold. NOTE: Spindle speed overrides may be altered during hold state.
                    // NOTE: sys.step_control.updateSpindleSpeed is automatically reset upon resume in step generator.
                    if (sys.step_control.updateSpindleSpeed) {
                        spindle->setState(restore_spindle, restore_spindle_speed);
                        sys.step_control.updateSpindleSpeed = false;
                    }
                }
            }
        }
        pollChannels();  // Handle realtime commands like status report, cycle start and reset
        protocol_exec_rt_system();
    }
}
