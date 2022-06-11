// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Report.h - Header for system level commands and real-time processes
*/

#include "Error.h"
#include "NutsBolts.h"
#include "Serial.h"  // CLIENT_xxx

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
    FileQuit        = 60,  // mc_reset was called during a file job
};

typedef uint8_t Counter;  // Report interval

extern Counter report_ovr_counter;
extern Counter report_wco_counter;

//function to notify
void _notify(const char* title, const char* msg);
void _notifyf(const char* title, const char* format, ...);

// Prints system status messages.
void report_status_message(Error status_code, Channel& channel);

// Prints miscellaneous feedback messages.
void report_feedback_message(Message message);

// Prints welcome message
void report_init_message(Print& channel);

// Prints an echo of the pre-parsed line received right before execution.
void report_echo_line_received(char* line, Print& channel);

// Prints realtime status report
void report_realtime_status(Channel& channel);

// Prints recorded probe position
void report_probe_parameters(Print& channel);

// Prints NGC parameters (coordinate offsets, probe)
void report_ngc_parameters(Print& channel);

// Prints current g-code parser mode state
void report_gcode_modes(Print& channel);

// Prints build info and user info
void report_build_info(const char* line, Print& channel);

#ifdef DEBUG_REPORT_REALTIME
void report_realtime_debug();
#endif

void reportTaskStackSize(UBaseType_t& saved);

void hex_msg(uint8_t* buf, const char* prefix, int len);

void addPinReport(char* status, char pinLetter);

#include "MyIOStream.h"

void        mpos_to_wpos(float* position);
const char* state_name();

extern const char* grbl_version;
extern const char* git_info;

// Callout to custom code
void display_init();
void display(const char* tag, String s);

extern bool readyNext;
