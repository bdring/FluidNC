// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Serial.cpp - Header for system level commands and real-time processes

  Original Grbl only supports communication via serial port. That is why this
  file is call serial.cpp. FluidNC supports many "channels".

  Channels are sources of commands like the serial port or a bluetooth connection.
  Multiple channels can be active at a time. If a channel asks for status, only the channel will
  receive the reply to the command.

  The serial port acts as the debugging port because it is always on and does not
  need to be reconnected after reboot. Messages about the configuration and other events
  are sent to the serial port automatically, without a request command. These are in the
  [MSG: xxxxxx] format which is part of the Grbl protocol.

  Important: It is up to the user that the channels play well together. Ideally, if one channel
  is sending the gcode, the others should just be doing status, feedhold, etc.

  Channels send line-oriented command (GCode, $$, [ESP...], etc) and realtime commands (?,!.~, etc)
  A line-oriented command is a string of printable characters followed by a '\r' or '\n'
  A realtime commands is a single character with no '\r' or '\n'

  After sending a line-oriented command, a sender must wait for an OK to send another.
  This is because only a certain number of commands can be buffered at a time.
  The system will tell you when it is ready for another one with the OK.

  Realtime commands can be sent at any time and will acted upon very quickly.
  Realtime commands can be anywhere in the stream.

  To allow the realtime commands to be randomly mixed in the stream of data, we
  read all channels as fast as possible. The realtime commands are acted upon and
  the other characters are placed into a per-channel buffer.  When a complete line
  is received, pollChannel returns the associated channel spec.
*/

#include "Serial.h"
#include "Uart.h"
#include "Machine/MachineConfig.h"
#include "WebUI/InputBuffer.h"
#include "WebUI/Commands.h"
#include "WebUI/WifiServices.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // rtSafetyDoor etc
#include "InputFile.h"
#include "WebUI/InputBuffer.h"  // XXX could this be a StringStream ?
#include "Main.h"               // display()
#include "StartupLog.h"         // startupLog

#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t channelCheckTaskHandle = 0;

void heapCheckTask(void* pvParameters) {
    static uint32_t heapSize = 0;
    while (true) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings and whatnot
        uint32_t newHeapSize = xPortGetFreeHeapSize();
        if (newHeapSize != heapSize) {
            heapSize = newHeapSize;
            log_info("heap " << heapSize);
        }
        vTaskDelay(3000 / portTICK_RATE_MS);  // Yield to other tasks

        static UBaseType_t uxHighWaterMark = 0;
#ifdef DEBUG_TASK_STACK
        reportTaskStackSize(uxHighWaterMark);
#endif
    }
}

// Act upon a realtime character
void execute_realtime_command(Cmd command, Channel& channel) {
    switch (command) {
        case Cmd::Reset:
            log_debug("Cmd::Reset");
            mc_reset();  // Call motion control reset routine.
            break;
        case Cmd::StatusReport:
            report_realtime_status(channel);  // direct call instead of setting flag
            break;
        case Cmd::CycleStart:
            rtCycleStart = true;
            break;
        case Cmd::FeedHold:
            rtFeedHold = true;
            break;
        case Cmd::SafetyDoor:
            rtSafetyDoor = true;
            break;
        case Cmd::JogCancel:
            if (sys.state == State::Jog) {  // Block all other states from invoking motion cancel.
                rtMotionCancel = true;
            }
            break;
        case Cmd::DebugReport:
#ifdef DEBUG_REPORT_REALTIME
            rtExecDebug = true;
#endif
            break;
        case Cmd::SpindleOvrStop:
            rtAccessoryOverride.bit.spindleOvrStop = 1;
            break;
        case Cmd::FeedOvrReset:
            rtFOverride = FeedOverride::Default;
            break;
        case Cmd::FeedOvrCoarsePlus:
            rtFOverride += FeedOverride::CoarseIncrement;
            if (rtFOverride > FeedOverride::Max) {
                rtFOverride = FeedOverride::Max;
            }
            break;
        case Cmd::FeedOvrCoarseMinus:
            rtFOverride -= FeedOverride::CoarseIncrement;
            if (rtFOverride < FeedOverride::Min) {
                rtFOverride = FeedOverride::Min;
            }
            break;
        case Cmd::FeedOvrFinePlus:
            rtFOverride += FeedOverride::FineIncrement;
            if (rtFOverride > FeedOverride::Max) {
                rtFOverride = FeedOverride::Max;
            }
            break;
        case Cmd::FeedOvrFineMinus:
            rtFOverride -= FeedOverride::FineIncrement;
            if (rtFOverride < FeedOverride::Min) {
                rtFOverride = FeedOverride::Min;
            }
            break;
        case Cmd::RapidOvrReset:
            rtROverride = RapidOverride::Default;
            break;
        case Cmd::RapidOvrMedium:
            rtROverride = RapidOverride::Medium;
            break;
        case Cmd::RapidOvrLow:
            rtROverride = RapidOverride::Low;
            break;
        case Cmd::RapidOvrExtraLow:
            rtROverride = RapidOverride::ExtraLow;
            break;
        case Cmd::SpindleOvrReset:
            rtSOverride = SpindleSpeedOverride::Default;
            break;
        case Cmd::SpindleOvrCoarsePlus:
            rtSOverride += SpindleSpeedOverride::CoarseIncrement;
            if (rtSOverride > SpindleSpeedOverride::Max) {
                rtSOverride = SpindleSpeedOverride::Max;
            }
            break;
        case Cmd::SpindleOvrCoarseMinus:
            rtSOverride -= SpindleSpeedOverride::CoarseIncrement;
            if (rtSOverride < SpindleSpeedOverride::Min) {
                rtSOverride = SpindleSpeedOverride::Min;
            }
            break;
        case Cmd::SpindleOvrFinePlus:
            rtSOverride += SpindleSpeedOverride::FineIncrement;
            if (rtSOverride > SpindleSpeedOverride::Max) {
                rtSOverride = SpindleSpeedOverride::Max;
            }
            break;
        case Cmd::SpindleOvrFineMinus:
            rtSOverride -= SpindleSpeedOverride::FineIncrement;
            if (rtSOverride < SpindleSpeedOverride::Min) {
                rtSOverride = SpindleSpeedOverride::Min;
            }
            break;
        case Cmd::CoolantFloodOvrToggle:
            rtAccessoryOverride.bit.coolantFloodOvrToggle = 1;
            break;
        case Cmd::CoolantMistOvrToggle:
            rtAccessoryOverride.bit.coolantMistOvrToggle = 1;
            break;
    }
}

// checks to see if a character is a realtime character
bool is_realtime_command(uint8_t data) {
    if (data >= 0x80) {
        return true;
    }
    auto cmd = static_cast<Cmd>(data);
    return cmd == Cmd::Reset || cmd == Cmd::StatusReport || cmd == Cmd::CycleStart || cmd == Cmd::FeedHold;
}

void AllChannels::init() {
    registration(&Uart0);               // USB Serial
    registration(&WebUI::inputBuffer);  // Macros
    registration(&startupLog);          // USB Serial
}

void AllChannels::registration(Channel* channel) {
    _channelq.push_back(channel);
}
void AllChannels::deregistration(Channel* channel) {
    _channelq.erase(std::remove(_channelq.begin(), _channelq.end(), channel), _channelq.end());
}

String AllChannels::info() {
    String retval;
    for (auto channel : _channelq) {
        retval += channel->name();
        retval += "\n";
    }
    return retval;
}

size_t AllChannels::write(uint8_t data) {
    for (auto channel : _channelq) {
        channel->write(data);
    }
    return 1;
}
size_t AllChannels::write(const uint8_t* buffer, size_t length) {
    for (auto channel : _channelq) {
        channel->write(buffer, length);
    }
    return length;
}
Channel* AllChannels::pollLine(char* line) {
    static Channel* lastChannel = nullptr;
    // To avoid starving other channels when one has a lot
    // of traffic, we poll the other channels before the last
    // one that returned a line.
    for (auto channel : _channelq) {
        // Skip the last channel in the loop
        if (channel != lastChannel && channel->pollLine(line)) {
            lastChannel = channel;
            return lastChannel;
        }
    }
    // If no other channel returned a line, try the last one
    if (lastChannel && lastChannel->pollLine(line)) {
        return lastChannel;
    }
    lastChannel = nullptr;
    return lastChannel;
}

AllChannels allChannels;

Channel* pollChannels(char* line) {
    // Throttle polling when we are not ready for a line, thus preventing
    // planner buffer starvation due to not calling Stepper::prep_buffer()
    // frequently enough, which is normally called periodically at the end
    // of protocol_exec_rt_system() via protocol_execute_realtime().
    static int counter = 0;
    if (line) {
        counter = 0;
    }
    if (counter > 0) {
        --counter;
        return nullptr;
    }
    counter = 50;

    Channel* retval = allChannels.pollLine(line);

    WebUI::COMMANDS::handle();      // Handles feeding watchdog and ESP restart
    WebUI::wifi_services.handle();  // OTA, web_server, telnet_server polling

    return retval;
}
