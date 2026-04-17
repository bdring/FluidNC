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
  is received, pollChannels returns the associated channel spec.
*/

#include "Serial.h"
#include "Machine/MachineConfig.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // *Event
#include "InputFile.h"
#include "Main.h"        // display()
#include "StartupLog.h"  // startupLog

#include "Driver/Console.h"
#include "Driver/fluidnc_gpio.h"

#include <atomic>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string_view>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

std::mutex AllChannels::_mutex_general;
std::mutex AllChannels::_mutex_pollLine;

std::vector<Channel*> AllChannels::snapshot_channels() {
    std::vector<Channel*> channels;

    std::lock_guard<std::mutex> lock(_mutex_general);
    channels.reserve(_channelq.size());
    for (auto channel : _channelq) {
        if (channel->try_acquire_processing_ref()) {
            channels.push_back(channel);
        }
    }

    return channels;
}

void AllChannels::reap_channels() {
    for (auto it = _zombies.begin(); it != _zombies.end();) {
        auto channel = *it;
        if (channel->pending_log_refs() || channel->pending_processing_refs()) {
            ++it;
            continue;
        }
        delete channel;
        it = _zombies.erase(it);
    }
}

void heapCheckTask(void* pvParameters) {
    static uint32_t heapSize = 0;
    while (true) {
        std::atomic_thread_fence(std::memory_order_seq_cst);  // read fence for settings and whatnot
        uint32_t newHeapSize = xPortGetFreeHeapSize();
        if (newHeapSize != heapSize) {
            heapSize = newHeapSize;
            log_info("heap " << heapSize);
        }
        vTaskDelay(3000 / portTICK_PERIOD_MS);  // Yield to other tasks

#ifdef DEBUG_TASK_STACK
        static UBaseType_t uxHighWaterMark = 0;
        reportTaskStackSize(uxHighWaterMark);
#endif
    }
}

void AllChannels::init() {
    registration(&startupLog);  // Early startup messages for $SS
}

void AllChannels::ready() {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->ready();
        channel->release_processing_ref();
    }
}

void AllChannels::kill(Channel* channel) {
    if (_killQueue) {
        xQueueSend(_killQueue, &channel, pdMS_TO_TICKS(10));
    }
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
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        log_stream(out, channel->name());
        channel->release_processing_ref();
    }
}

void AllChannels::flushRx() {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->flushRx();
        channel->release_processing_ref();
    }
}

size_t AllChannels::write(uint8_t data) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->write(data);
        channel->release_processing_ref();
    }
    return 1;
}
void AllChannels::notifyState(void) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->notifyState();
        channel->release_processing_ref();
    }
}

void AllChannels::notifyOvr(void) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->notifyOvr();
        channel->release_processing_ref();
    }
}

void AllChannels::notifyWco(void) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->notifyWco();
        channel->release_processing_ref();
    }
}
void AllChannels::notifyNgc(CoordIndex coord) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->notifyNgc(coord);
        channel->release_processing_ref();
    }
}

size_t AllChannels::write(const uint8_t* buffer, size_t length) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->write(buffer, length);
        channel->release_processing_ref();
    }
    return length;
}
void AllChannels::print_msg(MsgLevel level, const char* msg) {
    auto channels = snapshot_channels();
    for (auto channel : channels) {
        channel->print_msg(level, msg);
        channel->release_processing_ref();
    }
}

Channel* AllChannels::find(const std::string_view name) {
    _mutex_general.lock();
    for (auto channel : _channelq) {
        if (channel->name() == name) {
            _mutex_general.unlock();
            return channel;
        }
    }
    _mutex_general.unlock();
    return nullptr;
}
Channel* AllChannels::poll(char* line) {
    reap_channels();

    Channel* deadChannel;
    while (xQueueReceive(_killQueue, &deadChannel, 0)) {
        deadChannel->begin_closing();
        deregistration(deadChannel);
        _zombies.push_back(deadChannel);
    }

    reap_channels();

    // To avoid starving other channels when one has a lot
    // of traffic, we poll the other channels before the last
    // one that returned a line.
    _mutex_pollLine.lock();

    for (auto channel : _channelq) {
        // Skip the last channel in the loop

        if (channel == _lastChannel) {
            continue;
        }

        auto status = channel->pollLine(line);

        if (status == Error::Ok) {
            if (!channel->try_acquire_processing_ref()) {
                continue;
            }
            _lastChannel = channel;
            _mutex_pollLine.unlock();
            return _lastChannel;
        }
    }
    _mutex_pollLine.unlock();
    // If no other channel returned a line, try the last one
    if (_lastChannel) {
        auto status = _lastChannel->pollLine(line);
        if (status == Error::Ok) {
        if (!_lastChannel->try_acquire_processing_ref()) {
            _lastChannel = nullptr;
            return _lastChannel;
        }
        return _lastChannel;
        }
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
    static uint_fast8_t counter = 0;
    if (line) {
        counter = 0;
    }
    if (counter) {
        --counter;
        return nullptr;
    }
    counter = 50;

    Channel* retval = allChannels.poll(line);

    return retval;
}
