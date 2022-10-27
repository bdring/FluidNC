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

    size_t write(uint8_t data) override;
    String messages();
};

extern StartupLog startupLog;
