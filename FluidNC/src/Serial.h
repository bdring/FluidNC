// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Serial.h - Header for system level commands and real-time processes
*/

#include "Config.h"
#include "Channel.h"
#include <freertos/FreeRTOS.h>  // TickType_T
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdint.h>
#include <vector>

void channel_init();

Channel* pollChannels(char* line = nullptr);

class AllChannels : public Channel {
    std::vector<Channel*> _channelq;
    std::vector<Channel*> _zombies;

    Channel*      _lastChannel = nullptr;
    QueueHandle_t _killQueue;

    static SemaphoreHandle_t _mutex_general;
    static SemaphoreHandle_t _mutex_pollLine;

    void                  reap_channels();
    std::vector<Channel*> snapshot_channels();

public:
    // Depth covers the worst realistic burst: every WebSocket client plus the
    // telnet and HTTP-command channels being torn down between two poll cycles.
    AllChannels() : Channel("all") { _killQueue = xQueueCreate(32, sizeof(Channel*)); }

    // Queues a channel for deletion by the polling task.  Returns false if it
    // could not be queued (kill queue full, or never created); the caller still
    // owns the channel in that case and must retry.
    bool kill(Channel* channel);

    void registration(Channel* channel);
    void deregistration(Channel* channel);
    void init() override;
    void ready();

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    void print_msg(MsgLevel level, const char* msg) override;

    void flushRx() override;

    void notifyState();
    void notifyOvr();
    void notifyWco();
    void notifyNgc(CoordIndex coord);

    void listChannels(Channel& out);

    Channel* find(const std::string_view name);
    Channel* poll(char* line);
};

extern AllChannels allChannels;
