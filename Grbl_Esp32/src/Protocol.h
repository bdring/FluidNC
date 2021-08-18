#pragma once

/*
  Protocol.h - controls Grbl execution protocol and procedures
  Part of Grbl

  Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
  Copyright (c) 2009-2011 Simen Svale Skogsrud

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

// Line buffer size from the serial input stream to be executed.Also, governs the size of
// each of the startup blocks, as they are each stored as a string of this size.
//
// NOTE: Not a problem except for extreme cases, but the line buffer size can be too small
// and g-code blocks can get truncated. Officially, the g-code standards support up to 256
// characters. In future versions, this will be increased, when we know how much extra
// memory space we can invest into here or we re-write the g-code parser not to have this
// buffer.

const int LINE_BUFFER_SIZE = 256;

void protocol_reset();

// Starts Grbl main loop. It handles all incoming characters from the serial port and executes
// them as they complete. It is also responsible for finishing the initialization procedures.
void protocol_main_loop();

// Checks and executes a realtime command at various stop points in main program
void protocol_execute_realtime();
void protocol_exec_rt_system();

// Executes the auto cycle feature, if enabled.
void protocol_auto_cycle_start();

// Block until all buffered steps are executed
void protocol_buffer_synchronize();

// Executes the auto cycle feature, if enabled.
void protocol_auto_cycle_start();

// Disables the stepper motors or schedules it to happen
void protocol_disable_steppers();

extern volatile bool rtStatusReport;
extern volatile bool rtCycleStart;
extern volatile bool rtFeedHold;
extern volatile bool rtReset;
extern volatile bool rtSafetyDoor;
extern volatile bool rtMotionCancel;
extern volatile bool rtSleep;
extern volatile bool rtCycleStop;
extern volatile bool rtButtonMacro0;
extern volatile bool rtButtonMacro1;
extern volatile bool rtButtonMacro2;
extern volatile bool rtButtonMacro3;

#ifdef DEBUG_REPORT_REALTIME
extern volatile bool rtExecDebug;
#endif

// Override bit maps. Realtime bitflags to control feed, rapid, spindle, and coolant overrides.
// Spindle/coolant and feed/rapids are separated into two controlling flag variables.

struct AccessoryBits {
    uint8_t spindleOvrStop : 1;
    uint8_t coolantFloodOvrToggle : 1;
    uint8_t coolantMistOvrToggle : 1;
};

union Accessory {
    uint8_t       value;
    AccessoryBits bit;
};

extern volatile Accessory rtAccessoryOverride;  // Global realtime executor bitflag variable for spindle/coolant overrides.

extern volatile Percent rtFOverride;  // Feed override value in percent
extern volatile Percent rtROverride;  // Rapid feed override value in percent
extern volatile Percent rtSOverride;  // Spindle override value in percent

// Alarm codes.
enum class ExecAlarm : uint8_t {
    None               = 0,
    HardLimit          = 1,
    SoftLimit          = 2,
    AbortCycle         = 3,
    ProbeFailInitial   = 4,
    ProbeFailContact   = 5,
    HomingFailReset    = 6,
    HomingFailDoor     = 7,
    HomingFailPulloff  = 8,
    HomingFailApproach = 9,
    SpindleControl     = 10,
};

extern volatile ExecAlarm rtAlarm;  // Global realtime executor bitflag variable for setting various alarms.

#include <map>
extern std::map<ExecAlarm, const char*> AlarmNames;
