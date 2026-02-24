// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Serial.h - Header for system level commands and real-time processes
*/

#include "Config.h"
#include <vector>
#include <stdint.h>
#include "Channel.h"
#include "PlatformCompat.h"  // FreeRTOS compatibility (TickType_t, SemaphoreHandle_t, etc.)

void channel_init();

Channel* pollChannels(char* line = nullptr);

class AllChannels : public Channel {
    std::vector<Channel*> _channelq;

    Channel*      _lastChannel = nullptr;
    QueueHandle_t _killQueue;

    static SemaphoreHandle_t _mutex_general;
    static SemaphoreHandle_t _mutex_pollLine;

public:
    AllChannels() : Channel("all") { _killQueue = xQueueCreate(3, sizeof(Channel*)); }

    void kill(Channel* channel);

    void registration(Channel* channel);
    void deregistration(Channel* channel);
    void init() override;
    void ready();

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    void print_msg(MsgLevel level, const char* msg) override;

    void flushRx() override;

    void notifyOvr();
    void notifyWco();
    void notifyNgc(CoordIndex coord);

    void listChannels(Channel& out);

    Channel* find(const std::string_view name);
    Channel* poll(char* line);
};

extern AllChannels allChannels;
