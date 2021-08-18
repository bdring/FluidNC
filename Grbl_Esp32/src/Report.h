#pragma once

/*
  Report.h - Header for system level commands and real-time processes
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

#include "Error.h"
#include "NutsBolts.h"
#include "Exec.h"
#include "Serial.h"  // CLIENT_xxx

#include <cstdint>
#include <freertos/FreeRTOS.h>  // UBaseType_t

// With this enabled, Grbl sends back an echo of the line it has received, which has been pre-parsed (spaces
// removed, capitalized letters, no comments) and is to be immediately executed by Grbl. Echoes will not be
// sent upon a line buffer overflow, but should for all normal lines sent to Grbl. For example, if a user
// sendss the line 'g1 x1.032 y2.45 (test comment)', Grbl will echo back in the form '[echo: G1X1.032Y2.45]'.
// Only GCode lines are echoed, not command lines starting with $ or [ESP.
// NOTE: Only use this for debugging purposes!! When echoing, this takes up valuable resources and can effect
// performance. If absolutely needed for normal operation, the serial write buffer should be greatly increased
// to help minimize transmission waiting within the serial write protocol.
//#define DEBUG_REPORT_ECHO_LINE_RECEIVED // Default disabled. Uncomment to enable.

// This is similar to DEBUG_REPORT_ECHO_LINE_RECEIVED and subject to all its caveats,
// but instead of echoing the pre-parsed line, it echos the raw line exactly as
// received, including not only GCode lines, but also $ and [ESP commands.
//#define DEBUG_REPORT_ECHO_RAW_LINE_RECEIVED // Default disabled. Uncomment to enable.

// Define status reporting boolean enable bit flags in status_report_mask
enum RtStatus {
    Position = bitnum_to_mask(0),
    Buffer   = bitnum_to_mask(1),
};

const char* errorString(Error errorNumber);

// Define Grbl feedback message codes. Valid values (0-255).
enum class Message : uint8_t {
    CriticalEvent   = 1,
    AlarmLock       = 2,
    AlarmUnlock     = 3,
    Enabled         = 4,
    Disabled        = 5,
    SafetyDoorAjar  = 6,
    CheckLimits     = 7,
    ProgramEnd      = 8,
    RestoreDefaults = 9,
    SpindleRestore  = 10,
    SleepMode       = 11,
    ConfigAlarmLock = 12,
    SdFileQuit      = 60,  // mc_reset was called during an SD job
};

typedef uint8_t Counter;  // Report interval

extern Counter report_ovr_counter;
extern Counter report_wco_counter;

// functions to send data to the user.
void grbl_send(uint8_t client, const char* text);
void grbl_sendf(uint8_t client, const char* format, ...);
void info_client(uint8_t client, const char* format, ...);

//function to notify
void grbl_notify(const char* title, const char* msg);
void grbl_notifyf(const char* title, const char* format, ...);

// Prints system status messages.
void report_status_message(Error status_code, uint8_t client);
void report_realtime_steps();

// Prints system alarm messages.
void report_alarm_message(ExecAlarm alarm_code);

// Prints miscellaneous feedback messages.
void report_feedback_message(Message message);

// Prints welcome message
void report_init_message(uint8_t client);

// Prints Grbl help and current global settings
void report_grbl_help(uint8_t client);

// Prints Grbl global settings
void report_grbl_settings(uint8_t client, uint8_t show_extended);

// Prints an echo of the pre-parsed line received right before execution.
void report_echo_line_received(char* line, uint8_t client);

// Prints realtime status report
void report_realtime_status(uint8_t client);

// Prints recorded probe position
void report_probe_parameters(uint8_t client);

// Prints Grbl NGC parameters (coordinate offsets, probe)
void report_ngc_parameters(uint8_t client);

// Prints current g-code parser mode state
void report_gcode_modes(uint8_t client);

// Prints startup line when requested and executed.
void report_startup_line(uint8_t n, const char* line, uint8_t client);
void report_execute_startup_message(const char* line, Error status_code, uint8_t client);

// Prints build info and user info
void report_build_info(const char* line, uint8_t client);

void report_gcode_comment(char* comment);

#ifdef DEBUG_REPORT_REALTIME
void report_realtime_debug();
#endif

void report_machine_type(uint8_t client);

void report_hex_msg(char* buf, const char* prefix, int len);
void report_hex_msg(uint8_t* buf, const char* prefix, int len);

char* reportAxisLimitsMsg(uint8_t axis);
char* reportAxisNameMsg(uint8_t axis);
char* reportAxisNameMsg(uint8_t axis, uint8_t dual_axis);

void reportTaskStackSize(UBaseType_t& saved);

char*  report_state_text();
float* get_wco();
void   mpos_to_wpos(float* position);

void addPinReport(char* status, char pinLetter);

extern const char* dataBeginMarker;
extern const char* dataEndMarker;
