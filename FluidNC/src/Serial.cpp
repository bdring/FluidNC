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
#include "UartChannel.h"
#include "Machine/MachineConfig.h"
#include "WebUI/InputBuffer.h"
#include "WebUI/Commands.h"
#include "WebUI/WifiServices.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // *Event
#include "InputFile.h"
#include "WebUI/InputBuffer.h"  // XXX could this be a StringStream ?
#include "Main.h"               // display()
#include "StartupLog.h"         // startupLog

#include "Driver/fluidnc_gpio.h"

#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

std::mutex AllChannels::_mutex_general;
std::mutex AllChannels::_mutex_pollLine;

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
            protocol_send_event(&rtResetEvent);
            break;
        case Cmd::StatusReport:
            report_realtime_status(channel);  // direct call instead of setting flag
            // protocol_send_event(&reportStatusEvent, int(&channel));
            break;
        case Cmd::CycleStart:
            protocol_send_event(&cycleStartEvent);
            break;
        case Cmd::FeedHold:
            protocol_send_event(&feedHoldEvent);
            break;
        case Cmd::SafetyDoor:
            protocol_send_event(&safetyDoorEvent);
            break;
        case Cmd::JogCancel:
            if (sys.state == State::Jog) {  // Block all other states from invoking motion cancel.
                protocol_send_event(&motionCancelEvent);
            }
            break;
        case Cmd::DebugReport:
            protocol_send_event(&debugEvent);
            break;
        case Cmd::SpindleOvrStop:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::SpindleStopOvr);
            break;
        case Cmd::FeedOvrReset:
            protocol_send_event(&feedOverrideEvent, FeedOverride::Default);
            break;
        case Cmd::FeedOvrCoarsePlus:
            protocol_send_event(&feedOverrideEvent, FeedOverride::CoarseIncrement);
            break;
        case Cmd::FeedOvrCoarseMinus:
            protocol_send_event(&feedOverrideEvent, -FeedOverride::CoarseIncrement);
            break;
        case Cmd::FeedOvrFinePlus:
            protocol_send_event(&feedOverrideEvent, FeedOverride::FineIncrement);
            break;
        case Cmd::FeedOvrFineMinus:
            protocol_send_event(&feedOverrideEvent, -FeedOverride::FineIncrement);
            break;
        case Cmd::RapidOvrReset:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Default);
            break;
        case Cmd::RapidOvrMedium:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Medium);
            break;
        case Cmd::RapidOvrLow:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::Low);
            break;
        case Cmd::RapidOvrExtraLow:
            protocol_send_event(&rapidOverrideEvent, RapidOverride::ExtraLow);
            break;
        case Cmd::SpindleOvrReset:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::Default);
            break;
        case Cmd::SpindleOvrCoarsePlus:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::CoarseIncrement);
            break;
        case Cmd::SpindleOvrCoarseMinus:
            protocol_send_event(&spindleOverrideEvent, -SpindleSpeedOverride::CoarseIncrement);
            break;
        case Cmd::SpindleOvrFinePlus:
            protocol_send_event(&spindleOverrideEvent, SpindleSpeedOverride::FineIncrement);
            break;
        case Cmd::SpindleOvrFineMinus:
            protocol_send_event(&spindleOverrideEvent, -SpindleSpeedOverride::FineIncrement);
            break;
        case Cmd::CoolantFloodOvrToggle:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::FloodToggle);
            break;
        case Cmd::CoolantMistOvrToggle:
            protocol_send_event(&accessoryOverrideEvent, AccessoryOverride::MistToggle);
            break;
        case Cmd::Macro0:
            protocol_send_event(&macro0Event);
            break;
        case Cmd::Macro1:
            protocol_send_event(&macro1Event);
            break;
        case Cmd::Macro2:
            protocol_send_event(&macro2Event);
            break;
        case Cmd::Macro3:
            protocol_send_event(&macro3Event);
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
    registration(&WebUI::inputBuffer);  // Macros
    registration(&startupLog);          // Early startup messages for $SS
}

void AllChannels::kill(Channel* channel) {
    xQueueSend(_killQueue, &channel, 0);
}

void AllChannels::registration(Channel* channel) {
    _mutex_general.lock();
    _mutex_pollLine.lock();
    _channelq.push_back(channel);
    _mutex_pollLine.unlock();
    _mutex_general.unlock();
}
void AllChannels::deregistration(Channel* channel) {
    _mutex_general.lock();
    _mutex_pollLine.lock();
    if (channel == _lastChannel) {
        _lastChannel = nullptr;
    }
    _channelq.erase(std::remove(_channelq.begin(), _channelq.end(), channel), _channelq.end());
    _mutex_pollLine.unlock();
    _mutex_general.unlock();
}

void AllChannels::listChannels(Channel& out) {
    _mutex_general.lock();
    std::string retval;
    for (auto channel : _channelq) {
        log_to(out, channel->name());
    }
    _mutex_general.unlock();
}

void AllChannels::flushRx() {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->flushRx();
    }
    _mutex_general.unlock();
}

size_t AllChannels::write(uint8_t data) {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->write(data);
    }
    _mutex_general.unlock();
    return 1;
}
void AllChannels::notifyWco(void) {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->notifyWco();
    }
    _mutex_general.unlock();
}
void AllChannels::notifyNgc(CoordIndex coord) {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->notifyNgc(coord);
    }
    _mutex_general.unlock();
}

void AllChannels::stopJob() {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->stopJob();
    }
    _mutex_general.unlock();
}

size_t AllChannels::write(const uint8_t* buffer, size_t length) {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        channel->write(buffer, length);
    }
    _mutex_general.unlock();
    return length;
}
Channel* AllChannels::pollLine(char* line) {
    Channel* deadChannel;
    while (xQueueReceive(_killQueue, &deadChannel, 0)) {
        deregistration(deadChannel);
        delete deadChannel;
    }

    // To avoid starving other channels when one has a lot
    // of traffic, we poll the other channels before the last
    // one that returned a line.
    _mutex_pollLine.lock();

    for (auto channel : _channelq) {
        // Skip the last channel in the loop
        if (channel != _lastChannel && channel->pollLine(line)) {
            _lastChannel = channel;
            _mutex_pollLine.unlock();
            return _lastChannel;
        }
    }
    _mutex_pollLine.unlock();
    // If no other channel returned a line, try the last one
    if (_lastChannel && _lastChannel->pollLine(line)) {
        return _lastChannel;
    }
    _lastChannel = nullptr;
    return _lastChannel;
}

AllChannels allChannels;

Channel* pollChannels(char* line) {
    poll_gpios();
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

    WebUI::COMMANDS::handle();      // Handles ESP restart
    WebUI::wifi_services.handle();  // OTA, webServer, telnetServer polling

    return retval;
}
