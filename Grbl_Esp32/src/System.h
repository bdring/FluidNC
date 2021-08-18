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

#include "Types.h"

// Execution states and alarm
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

enum class Override : uint8_t {
    ParkingMotion = 0,  // M56 (Default: Must be zero)
    Disabled      = 1,  // Parking disabled.
};

// Global system variables
struct system_t {
    volatile State state;              // Tracks the current system state of Grbl.
    bool           abort;              // System abort flag. Forces exit back to main loop for reset.
    Suspend        suspend;            // System suspend bitflag variable that manages holds, cancels, and safety door.
    StepControl    step_control;       // Governs the step segment generator depending on system state.
    Percent        f_override;         // Feed rate override value in percent
    Percent        r_override;         // Rapids override value in percent
    Percent        spindle_speed_ovr;  // Spindle speed value in percent
    Override       override_ctrl;      // Tracks override control states.
    SpindleSpeed   spindle_speed;
};
extern system_t sys;

// NOTE: These position variables may need to be declared as volatiles, if problems arise.
extern int32_t motor_steps[MAX_N_AXIS];  // Real-time machine (aka home) position vector in steps.
extern int32_t probe_steps[MAX_N_AXIS];  // Last probe position in machine coordinates and steps.

void system_reset();

// Execute the startup script lines stored in non-volatile storage upon initialization
void  system_execute_startup();
Error execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel auth_level);
Error system_execute_line(char* line, WebUI::ESPResponseStream*, WebUI::AuthenticationLevel);
Error system_execute_line(char* line, uint8_t client, WebUI::AuthenticationLevel);
Error do_command_or_setting(const char* key, char* value, WebUI::AuthenticationLevel auth_level, WebUI::ESPResponseStream*);

float   steps_to_mpos(int32_t steps, uint8_t axis);
int32_t mpos_to_steps(float mpos, size_t axis);

// Updates a machine position array from a steps array
void   motor_steps_to_mpos(float* position, int32_t* steps);
float* get_mpos();
