// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Report.h - Header for system level commands and real-time processes
*/

#include "Error.h"
#include "NutsBolts.h"
#include "Protocol.h"  // ExecAlarm
#include "Serial.h"    // CLIENT_xxx

#include <cstdint>
#include <freertos/FreeRTOS.h>  // UBaseType_t

// Enabling this sends back an echo of each received line, which has been pre-parsed (spaces
// removed, capitalized letters, no comments) prior to its execution. Echoes will not be
// sent upon a line buffer overflow. For example, if a user
// sendss the line 'g1 x1.032 y2.45 (test comment)', it will be echoed in the form '[echo: G1X1.032Y2.45]'.
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

// Define feedback message codes. Valid values (0-255).
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
void _send(uint8_t client, const char* text);
void _sendf(uint8_t client, const char* format, ...);
void info_client(uint8_t client, const char* format, ...);

//function to notify
void _notify(const char* title, const char* msg);
void _notifyf(const char* title, const char* format, ...);

// Prints system status messages.
void report_status_message(Error status_code, uint8_t client);
void report_realtime_steps();

// Prints system alarm messages.
void report_alarm_message(ExecAlarm alarm_code);

// Prints miscellaneous feedback messages.
void report_feedback_message(Message message);

// Prints welcome message
void report_init_message(uint8_t client);

// Prints help and current global settings
void report_help(uint8_t client);

// Prints global settings
void report_settings(uint8_t client, uint8_t show_extended);

// Prints an echo of the pre-parsed line received right before execution.
void report_echo_line_received(char* line, uint8_t client);

// Prints realtime status report
void report_realtime_status(uint8_t client);

// Prints recorded probe position
void report_probe_parameters(uint8_t client);

// Prints NGC parameters (coordinate offsets, probe)
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
