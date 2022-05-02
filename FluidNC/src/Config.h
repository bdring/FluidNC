// Copyright (c) 2012-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2021 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

// This file contains compile-time configuration choices.  Most users will not need
// to directly modify these, but they are here for unusual needs, i.e.
// performance tuning or adjusting to non-typical machines.

// IMPORTANT: Any changes here require recompilation.

/*
ESP 32 Notes

Some features should not be changed. See notes below.

*/

#include "NutsBolts.h"

// It is no longer necessary to edit this file to choose
// a machine configuration; edit machine.h instead
// machine.h is #included below, after some definitions
// that the machine file might choose to undefine.

const int SUPPORT_TASK_CORE = 1;  // Reference: CONFIG_ARDUINO_RUNNING_CORE = 1

const int MAX_N_AXIS = 6;

// Serial baud rate
// OK to change, but the ESP32 boot text is 115200, so you will not see that is your
// serial monitor, sender, etc uses a different value than 115200
const int BAUD_RATE = 115200;

//Connect to your local AP with these credentials
//#define CONNECT_TO_SSID  "your SSID"
//#define SSID_PASSWORD  "your SSID password"
//CONFIGURE_EYECATCH_BEGIN (DO NOT MODIFY THIS LINE)

// #define ENABLE_BLUETOOTH  // enable bluetooth -- platformio: pio run -e bt
// #define ENABLE_WIFI       // enable wifi      -- platformio: pio run -e wifi

// Warning! The current authentication implementation is too weak to provide
// security against an attacker, since passwords are stored and transmitted
// "in the clear" over unsecured channels.  It should be treated as a
// "friendly suggestion" to prevent unwitting dangerous actions, rather than
// as effective security against malice.
// #define ENABLE_AUTHENTICATION
// CONFIGURE_EYECATCH_END (DO NOT MODIFY THIS LINE)

#ifdef ENABLE_AUTHENTICATION
const char* const DEFAULT_ADMIN_PWD   = "admin";
const char* const DEFAULT_USER_PWD    = "user";
const char* const DEFAULT_ADMIN_LOGIN = "admin";
const char* const DEFAULT_USER_LOGIN  = "user";
#endif

// Upon a successful probe cycle, this option provides immediately feedback of the probe coordinates
// through an automatically generated message. If disabled, users can still access the last probe
// coordinates through the '$#' print parameters command.
const bool MESSAGE_PROBE_COORDINATES = true;  // Enabled by default. Comment to disable.

// When the microprocess is power cycled or hard reset with the reset button, the default boot state
// is no ALARM.  This makes it simpler for new users.  When homing is enabled and limit switches are
// present, the initial state on boot is ALARM, indicatint that the position is unknown, so the user
// must home the system before proceeding. The FORCE_INITIALIZATION option overrides the no-ALARM
// behavior, so the system boots into ALARM state regardless of homing presence. This option is
// provided for OEMs and LinuxCNC users that prefer this behavior.
const bool FORCE_INITIALIZATION_ALARM = false;  // Default disabled. Uncomment to enable.

// ---------------------------------------------------------------------------------------
// ADVANCED CONFIGURATION OPTIONS:

// Configure rapid, feed, and spindle override settings. These values define the max and min
// allowable override values and the coarse and fine increments per command received. Please
// note the allowable values in the descriptions following each define.
namespace FeedOverride {
    const int Default         = 100;  // 100%. Don't change this value.
    const int Max             = 200;  // Percent of programmed feed rate (100-255). Usually 120% or 200%
    const int Min             = 10;   // Percent of programmed feed rate (1-100). Usually 50% or 1%
    const int CoarseIncrement = 10;   // (1-99). Usually 10%.
    const int FineIncrement   = 1;    // (1-99). Usually 1%.
};
namespace RapidOverride {
    const int Default  = 100;  // 100%. Don't change this value.
    const int Medium   = 50;   // Percent of rapid (1-99). Usually 50%.
    const int Low      = 25;   // Percent of rapid (1-99). Usually 25%.
    const int ExtraLow = 5;    // Percent of rapid (1-99). Usually 5%.  Not Supported
};

namespace SpindleSpeedOverride {
    const int Default         = 100;  // 100%. Don't change this value.
    const int Max             = 200;  // Percent of programmed spindle speed (100-255). Usually 200%.
    const int Min             = 10;   // Percent of programmed spindle speed (1-100). Usually 10%.
    const int CoarseIncrement = 10;   // (1-99). Usually 10%.
    const int FineIncrement   = 1;    // (1-99). Usually 1%.
}

// When a M2 or M30 program end command is executed, most GCode states are restored to their defaults.
// This compile-time option includes the restoring of the feed, rapid, and spindle speed override values
// to their default values at program end.
const bool RESTORE_OVERRIDES_AFTER_PROGRAM_END = true;  // Default enabled. Comment to disable.

// Some status report data isn't necessary for realtime, only intermittently, because the values don't
// change often. The following macros configures how many times a status report needs to be called before
// the associated data is refreshed and included in the status report. However, if one of these value
// changes, this data will be included in the next status report, regardless of the current count.
// This reduces the communication overhead of high frequency reporting and agressive streaming.
// The busy and idle refresh counts send refreshes more frequently when not doing anything important.
// With a good GUI, this data doesn't need to be refreshed very often, on the order of a several seconds.
// NOTE: WCO refresh must be 2 or greater. OVR refresh must be 1 or greater.

const int REPORT_OVR_REFRESH_BUSY_COUNT = 20;  // (1-255)
const int REPORT_OVR_REFRESH_IDLE_COUNT = 10;  // (1-255) Must be less than or equal to the busy count
const int REPORT_WCO_REFRESH_BUSY_COUNT = 30;  // (2-255)
const int REPORT_WCO_REFRESH_IDLE_COUNT = 10;  // (2-255) Must be less than or equal to the busy count

// The temporal resolution of the acceleration management subsystem. A higher number gives smoother
// acceleration, particularly noticeable on machines that run at very high feedrates, but may negatively
// impact performance. The correct value for this parameter is machine dependent, so it's advised to
// set this only as high as needed. Approximate successful values can widely range from 50 to 200 or more.
// NOTE: Changing this value also changes the execution time of a segment in the step segment buffer.
// When increasing this value, this stores less overall time in the segment buffer and vice versa. Make
// certain the step segment buffer is increased/decreased to account for these changes.
const int ACCELERATION_TICKS_PER_SECOND = 100;

// Sets which axis the tool length offset is applied. Assumes the spindle is always parallel with
// the selected axis with the tool oriented toward the negative direction. In other words, a positive
// tool length offset value is subtracted from the current location.
const int TOOL_LENGTH_OFFSET_AXIS = Z_AXIS;  // Default z-axis. Valid values are X_AXIS, Y_AXIS, or Z_AXIS.

// Minimum planner junction speed. Sets the default minimum junction speed the planner plans to at
// every buffer block junction, except for starting from rest and end of the buffer, which are always
// zero. This value controls how fast the machine moves through junctions with no regard for acceleration
// limits or angle between neighboring block line move directions. This is useful for machines that can't
// tolerate the tool dwelling for a split second, i.e. 3d printers or laser cutters. If used, this value
// should not be much greater than zero or to the minimum value necessary for the machine to work.
const float MINIMUM_JUNCTION_SPEED = 0.0f;  // (mm/min)

// Sets the minimum feed rate the planner will allow. Any value below it will be set to this minimum
// value. This also ensures that a planned motion always completes and accounts for any floating-point
// round-off errors. Although not recommended, a lower value than 1.0 mm/min will likely work in smaller
// machines, perhaps to 0.1mm/min, but your success may vary based on multiple factors.
const double MINIMUM_FEED_RATE = 1.0;  // (mm/min)

// Number of arc generation iterations by small angle approximation before exact arc trajectory
// correction with expensive sin() and cos() calcualtions. This parameter maybe decreased if there
// are issues with the accuracy of the arc generations, or increased if arc execution is getting
// bogged down by too many trig calculations.
const int N_ARC_CORRECTION = 12;  // Integer (1-255)

// The arc G2/3 GCode standard is problematic by definition. Radius-based arcs have horrible numerical
// errors when arc at semi-circles(pi) or full-circles(2*pi). Offset-based arcs are much more accurate
// but still have a problem when arcs are full-circles (2*pi). This define accounts for the floating
// point issues when offset-based arcs are commanded as full circles, but get interpreted as extremely
// small arcs with around machine epsilon (1.2e-7rad) due to numerical round-off and precision issues.
// This define value sets the machine epsilon cutoff to determine if the arc is a full-circle or not.
// NOTE: Be very careful when adjusting this value. It should always be greater than 1.2e-7 but not too
// much greater than this. The default setting should capture most, if not all, full arc error situations.
const double ARC_ANGULAR_TRAVEL_EPSILON = 5E-7;  // Float (radians)

// Serial send and receive buffer size. The receive buffer is often used as another streaming
// buffer to store incoming blocks to be processed when ready. Most streaming
// interfaces will character count and track each block send to each block response. So,
// increase the receive buffer if a deeper receive buffer is needed for streaming and avaiable
// memory allows. The send buffer primarily handles messages. Only increase if large
// messages are sent and the system begins to stall, waiting to send the rest of the message.
// #define RX_BUFFER_SIZE 128 // (1-254) Uncomment to override defaults in serial.h
// #define TX_BUFFER_SIZE 100 // (1-254)

// Writing to non-volatile storage (NVS) can take a long time and interfere with timely instruction
// execution, causing problems for the stepper ISRs and serial comm ISRs and subsequent loss of
// stepper position and serial data. This configuration option forces the planner buffer to completely
// empty whenever the NVS is written, to prevent any chance of lost steps.
// It doesn't prevent loss of serial Rx data, especially if a GUI is premptively filling up the
// serial Rx buffer.  GUIs should detect GCodes that write to NVS - notably G10,G28.1,G30.1 -
// and wait for an 'ok' before sending more data.
// NOTE: Most setting changes - $ commands - are blocked when a job is running. Coordinate setting
// GCode commands (G10,G28/30.1) are not blocked, since they are part of an active streaming job.
// This option forces a planner buffer sync only with such GCode commands.
const bool FORCE_BUFFER_SYNC_DURING_NVS_WRITE = true;  // Default enabled. Comment to disable.

// In old versions of Grbl, v0.9 and prior, there is a bug where the `WPos:` work position reported
// may not correlate to what is executing, because `WPos:` is based on the GCode parser state, which
// can be several motions behind. This option forces the planner buffer to empty, sync, and stop
// motion whenever there is a command that alters the work coordinate offsets `G10,G43.1,G92,G54-59`.
// This is the simplest way to ensure `WPos:` is always correct. Fortunately, it's exceedingly rare
// that any of these commands are used need continuous motions through them.
const bool FORCE_BUFFER_SYNC_DURING_WCO_CHANGE = true;  // Default enabled. Comment to disable.

// By default, feed rate overrides are disabled for all G38.x probe cycle commands. Although this
// may be different than some pro-class machine control, it's arguable that it should be this way.
// Most probe sensors produce different levels of error that is dependent on rate of speed. By
// keeping probing cycles to their programmed feed rates, the probe sensor should be a lot more
// repeatable. If needed, you can disable this behavior by uncommenting the define below.
const bool ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES = false;

// Configure options for the parking motion, if enabled.
#define PARKING_AXIS Z_AXIS                      // Define which axis that performs the parking motion
const double PARKING_TARGET            = -5.0;   // Parking axis target. In mm, as machine coordinate.
const double PARKING_RATE              = 800.0;  // Parking fast rate after pull-out in mm/min.
const double PARKING_PULLOUT_RATE      = 250.0;  // Pull-out/plunge slow feed rate in mm/min.
const double PARKING_PULLOUT_INCREMENT = 5.0;    // Spindle pull-out and plunge distance in mm. Incremental distance.
// Must be positive value or equal to zero.

// INCLUDE_OLED_IO enables access to a basic OLED library.  To use it you must uncomment the
//  "thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays" line in platformio.ini
// You must uncomment it if you use either INCLUDE_OLED_TINY or INCLUDE_OLED_BASIC
// #define INCLUDE_OLED_IO

// INCLUDE_OLED_TINY includes a driver for a very small 64x48 OLED display
// #define INCLUDE_OLED_TINY

// INCLUDE_OLED_BASIC includes a driver for a modest sized OLED display
// #define INCLUDE_OLED_BASIC

// #define DEBUG_STEPPING
