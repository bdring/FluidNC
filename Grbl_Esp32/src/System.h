#pragma once

/*
  System.h - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

    2018 -	Bart Dring This file was modifed for use on the ESP32
        CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

// Execution states and alarm
#include "Exec.h"
#include "Probe.h"
#include "Error.h"
#include "Config.h"  // MAX_AXIS
#include "WebUI/Authentication.h"
#include "WebUI/ESPResponse.h"

// System states. The state variable primarily tracks the individual functions
// of Grbl to manage each without overlapping. It is also used as a messaging flag for
// critical events.
enum class State : uint8_t {
    Idle = 0,     // Must be zero.
    Alarm,        // In alarm state. Locks out all g-code processes. Allows settings access.
    CheckMode,    // G-code check mode. Locks out planner and motion only.
    Homing,       // Performing homing cycle
    Cycle,        // Cycle is running or motions are being executed.
    Hold,         // Active feed hold
    Jog,          // Jogging mode.
    SafetyDoor,   // Safety door is ajar. Feed holds and de-energizes system.
    Sleep,        // Sleep state.
    ConfigAlarm,  // You can't do anything but fix your config file.
};

extern std::map<State, const char*> StateName;

// Step segment generator state flags.
struct StepControl {
    uint8_t endMotion : 1;
    uint8_t executeHold : 1;
    uint8_t executeSysMotion : 1;
    uint8_t updateSpindleSpeed : 1;
};

// System suspend flags. Used in various ways to manage suspend states and procedures.
struct SuspendBits {
    uint8_t holdComplete : 1;     // Indicates initial feed hold is complete.
    uint8_t restartRetract : 1;   // Flag to indicate a retract from a restore parking motion.
    uint8_t retractComplete : 1;  // (Safety door only) Indicates retraction and de-energizing is complete.
    uint8_t initiateRestore : 1;  // (Safety door only) Flag to initiate resume procedures from a cycle start.
    uint8_t restoreComplete : 1;  // (Safety door only) Indicates ready to resume normal operation.
    uint8_t safetyDoorAjar : 1;   // Tracks safety door state for resuming.
    uint8_t motionCancel : 1;     // Indicates a canceled resume motion. Currently used by probing routine.
    uint8_t jogCancel : 1;        // Indicates a jog cancel in process and to reset buffers when complete.
};
union Suspend {
    uint8_t     value;
    SuspendBits bit;
};

typedef uint32_t MotorMask;  // Bits indexed by motor_num*16 + axis
typedef uint16_t AxisMask;   // Bits indexed by axis number
typedef uint8_t  Percent;    // Integer percent
typedef uint8_t  Counter;    // Report interval

enum class Override : uint8_t {
    ParkingMotion = 0,  // M56 (Default: Must be zero)
    Disabled      = 1,  // Parking disabled.
};

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

// Global system variables
struct system_t {
    volatile State state;               // Tracks the current system state of Grbl.
    bool           abort;               // System abort flag. Forces exit back to main loop for reset.
    Suspend        suspend;             // System suspend bitflag variable that manages holds, cancels, and safety door.
    StepControl    step_control;        // Governs the step segment generator depending on system state.
    bool           probe_succeeded;     // Tracks if last probing cycle was successful.
    Percent        f_override;          // Feed rate override value in percent
    Percent        r_override;          // Rapids override value in percent
    Percent        spindle_speed_ovr;   // Spindle speed value in percent
    SpindleStop    spindle_stop_ovr;    // Tracks spindle stop override states
    Counter        report_ovr_counter;  // Tracks when to add override data to status reports.
    Counter        report_wco_counter;  // Tracks when to add work coordinate offset data to status reports.
    Override       override_ctrl;       // Tracks override control states.
    SpindleSpeed   spindle_speed;
};

extern system_t sys;

// NOTE: These position variables may need to be declared as volatiles, if problems arise.
extern int32_t sys_position[MAX_N_AXIS];        // Real-time machine (aka home) position vector in steps.
extern int32_t sys_probe_position[MAX_N_AXIS];  // Last probe position in machine coordinates and steps.

extern volatile ProbeState    sys_probe_state;    // Probing state value.  Used to coordinate the probing cycle with stepper ISR.
extern volatile ExecAlarm     sys_rt_exec_alarm;  // Global realtime executor bitflag variable for setting various alarms.
extern volatile ExecAccessory sys_rt_exec_accessory_override;  // Global realtime executor bitflag variable for spindle/coolant overrides.
extern volatile Percent       sys_rt_f_override;               // Feed override value in percent
extern volatile Percent       sys_rt_r_override;               // Rapid feed override value in percent
extern volatile Percent       sys_rt_s_override;               // Spindle override value in percent
extern volatile bool          rtStatusReport;
extern volatile bool          rtCycleStart;
extern volatile bool          rtFeedHold;
extern volatile bool          rtReset;
extern volatile bool          rtSafetyDoor;
extern volatile bool          rtMotionCancel;
extern volatile bool          rtSleep;
extern volatile bool          rtCycleStop;
extern volatile bool          rtButtonMacro0;
extern volatile bool          rtButtonMacro1;
extern volatile bool          rtButtonMacro2;
extern volatile bool          rtButtonMacro3;
#ifdef DEBUG_REPORT_REALTIME
extern volatile bool sys_rt_exec_debug;
#endif

void init_output_pins();  // Renamed from system_init() due to conflict with esp32 files

void system_reset();

// Execute the startup script lines stored in non-volatile storage upon initialization
void  system_execute_startup();
Error execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel auth_level);
Error system_execute_line(char* line, WebUI::ESPResponseStream*, WebUI::AuthenticationLevel);
Error system_execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel);
Error do_command_or_setting(const char* key, char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream*);
void  system_flag_wco_change();

// Returns machine position of axis 'idx'. Must be sent a 'step' array.
float system_convert_axis_steps_to_mpos(int32_t* steps, uint8_t idx);

// Updates a machine 'position' array based on the 'step' array sent.
void   system_convert_array_steps_to_mpos(float* position, int32_t* steps);
float* system_get_mpos();


int8_t sys_get_next_PWM_chan_num();
