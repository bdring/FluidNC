// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Report.h - Header for system level commands and real-time processes
*/

#include "Error.h"
#include "Config.h"
#include "Serial.h"  // CLIENT_xxx

#include <cstdint>
#include <freertos/FreeRTOS.h>  // UBaseType_t

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
    HardStop        = 13,
    FileQuit        = 60,  // mc_critical was called during a file job
};

typedef uint8_t Counter;  // Report interval

extern Counter report_ovr_counter;
extern Counter report_wco_counter;

//function to notify
void _notify(const char* title, const char* msg);
void _notifyf(const char* title, const char* format, ...);

// Prints miscellaneous feedback messages.
void report_feedback_message(Message message);
void report_error_message(Message message);

// Prints welcome message
void report_init_message(Channel& channel);

// Prints an echo of the pre-parsed line received right before execution.
void report_echo_line_received(char* line, Channel& channel);

// Prints realtime status report
void report_realtime_status(Channel& channel);

// Prints recorded probe position
void report_probe_parameters(Channel& channel);

void report_ngc_coord(CoordIndex coord, Channel& channel);

// Prints NGC parameters (coordinate offsets, probe)
void report_ngc_parameters(Channel& channel);

// Prints current g-code parser mode state
void report_gcode_modes(Channel& channel);

// Prints build info and user info
void report_build_info(const char* line, Channel& channel);

void report_realtime_debug();

void reportTaskStackSize(UBaseType_t& saved);

void hex_msg(uint8_t* buf, const char* prefix, int len);

void addPinReport(char* status, char pinLetter);

#include "MyIOStream.h"

void        mpos_to_wpos(float* position);
const char* state_name();

extern const char* grbl_version;
extern const char* git_info;
extern const char* git_url;

// Callout to custom code
void display_init();

extern bool readyNext;
