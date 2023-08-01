// Copyright (c) 2011-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2009-2011 Simen Svale Skogsrud
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Types.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"

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

void protocol_init();

// Starts the main loop. It handles all incoming characters from the serial port and executes
// them as they complete. It is also responsible for finishing the initialization procedures.
void protocol_main_loop();

// Checks and executes a realtime command at various stop points in main program
void protocol_execute_realtime();
void protocol_exec_rt_system();

// Executes the auto cycle feature, if enabled.
void protocol_auto_cycle_start();

// Block until all buffered steps are executed
void protocol_buffer_synchronize();

// Disables the stepper motors or schedules it to happen
void protocol_disable_steppers();
void protocol_cancel_disable_steppers();

extern volatile bool rtCycleStop;

extern volatile bool runLimitLoop;

// Alarm codes.
enum class ExecAlarm : uint8_t {
    None                  = 0,
    HardLimit             = 1,
    SoftLimit             = 2,
    AbortCycle            = 3,
    ProbeFailInitial      = 4,
    ProbeFailContact      = 5,
    HomingFailReset       = 6,
    HomingFailDoor        = 7,
    HomingFailPulloff     = 8,
    HomingFailApproach    = 9,
    SpindleControl        = 10,
    ControlPin            = 11,
    HomingAmbiguousSwitch = 12,
    HardStop              = 13,
    Unhomed               = 14,
    Init                  = 15,
};

extern volatile ExecAlarm lastAlarm;

#include <map>
extern std::map<ExecAlarm, const char*> AlarmNames;

const char* alarmString(ExecAlarm alarmNumber);

#include "Event.h"
enum AccessoryOverride {
    SpindleStopOvr = 1,
    FloodToggle    = 2,
    MistToggle     = 3,
};

extern ArgEvent feedOverrideEvent;
extern ArgEvent rapidOverrideEvent;
extern ArgEvent spindleOverrideEvent;
extern ArgEvent accessoryOverrideEvent;
extern ArgEvent limitEvent;
extern ArgEvent faultPinEvent;

extern ArgEvent reportStatusEvent;

extern NoArgEvent safetyDoorEvent;
extern NoArgEvent feedHoldEvent;
extern NoArgEvent cycleStartEvent;
extern NoArgEvent cycleStopEvent;
extern NoArgEvent motionCancelEvent;
extern NoArgEvent sleepEvent;
extern NoArgEvent rtResetEvent;
extern NoArgEvent debugEvent;
extern NoArgEvent unhomedEvent;
extern NoArgEvent startEvent;
extern NoArgEvent restartEvent;

extern NoArgEvent runStartupLinesEvent;

// extern NoArgEvent statusReportEvent;

extern xQueueHandle event_queue;

extern bool pollingPaused;

struct EventItem {
    Event* event;
    void*  arg;
};

void protocol_send_event(Event*, void* arg = 0);
void protocol_handle_events();

void send_alarm(ExecAlarm alarm);
void send_alarm_from_ISR(ExecAlarm alarm);

inline void protocol_send_event(Event* evt, int arg) {
    protocol_send_event(evt, (void*)arg);
}

void protocol_send_event_from_ISR(Event* evt, void* arg = 0);

void send_line(Channel& channel, const char* message);
void send_line(Channel& channel, const std::string* message);
void send_line(Channel& channel, const std::string& message);

void drain_messages();

extern uint32_t heapLowWater;
