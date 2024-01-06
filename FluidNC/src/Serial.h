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
#include <freertos/FreeRTOS.h>  // TickType_T
#include <freertos/queue.h>
#include <mutex>

// See if the character is an action command like feedhold or jogging. If so, do the action and return true
uint8_t check_action_command(uint8_t data);

void channel_init();

Channel* pollChannels(char* line = nullptr);

class AllChannels : public Channel {
    std::vector<Channel*> _channelq;

    Channel*     _lastChannel = nullptr;
    xQueueHandle _killQueue;

    static std::mutex _mutex_general;
    static std::mutex _mutex_pollLine;

public:
    AllChannels() : Channel("all") { _killQueue = xQueueCreate(3, sizeof(Channel*)); }

    void kill(Channel* channel);

    void registration(Channel* channel);
    void deregistration(Channel* channel);
    void init();
    void ready();

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    void print_msg(MsgLevel level, const char* msg) override;

    void flushRx();

    void notifyWco();
    void notifyNgc(CoordIndex coord);

    void listChannels(Channel& out);

    Channel* pollLine(char* line) override;
    Channel* find(const std::string& name);

    void stopJob() override;
};

extern AllChannels allChannels;
