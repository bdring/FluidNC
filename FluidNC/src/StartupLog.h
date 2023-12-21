// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Channel.h"

class StartupLog : public Channel {
public:
    StartupLog() : Channel("Startup Log") {}
    virtual ~StartupLog();

    size_t write(uint8_t data) override;

    static void init();
    static void dump(Channel& channel);
};

extern StartupLog startupLog;
