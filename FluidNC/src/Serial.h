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
    // Channel registrations and deletions requested from other tasks, applied
    // by the polling task in the order they were queued (see poll()).
    struct ChannelOp {
        enum class Kind : uint8_t { Add, Kill } kind;
        Channel* channel;
    };
    QueueHandle_t _opQueue;
    bool          queue_op(ChannelOp::Kind kind, Channel* channel);

    static SemaphoreHandle_t _mutex_general;
    static SemaphoreHandle_t _mutex_pollLine;

    void                  reap_channels();
    std::vector<Channel*> snapshot_channels();

public:
    // Depth covers the worst realistic burst: every WebSocket client plus the
    // telnet and HTTP-command channels being torn down between two poll cycles,
    // with room for WebSocket connects queued alongside them.
    AllChannels() : Channel("all") { _opQueue = xQueueCreate(32, sizeof(ChannelOp)); }

    // Queues a new channel to be registered by the polling task at the start
    // of its next pass, before it takes the channel locks.  For callers that
    // run under another library's lock: the WebSocket connect event runs under
    // AsyncWebSocket's lock, while the polling task takes that same lock (via
    // AsyncWebSocket::client()) when it flushes WebSocket output under
    // _mutex_pollLine, so calling registration() there can deadlock both
    // tasks.  Adds and kills share one queue, so a kill queued after an add
    // of the same channel is always applied after it.  Until the polling task
    // applies the add, the channel gets no polling and misses broadcasts.
    // Returns false if it could not be queued; the caller still owns the channel.
    bool register_later(Channel* channel);

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
