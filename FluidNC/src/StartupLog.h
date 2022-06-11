// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Channel.h"
#include <queue>

class StartupLog : public Channel {
private:
    String _messages;

public:
    StartupLog(const char* name) : Channel(name) {}
    virtual ~StartupLog();

    int    available(void) override { return 0; }
    int    read(void) override { return -1; }
    size_t readBytes(char* buffer, size_t length) override { return 0; };
    int    peek(void) override { return -1; }
    size_t write(uint8_t data) override;

    Channel* pollLine(char* line) override { return nullptr; }

    int    rx_buffer_available() { return 0; }
    String messages();
};

extern StartupLog startupLog;
