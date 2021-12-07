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

// See if the character is an action command like feedhold or jogging. If so, do the action and return true
uint8_t check_action_command(uint8_t data);

void channel_init();

// Define realtime command special characters. These characters are 'picked-off' directly from the
// serial read data stream and are not passed to the grbl line execution parser. Select characters
// that do not and must not exist in the streamed GCode program. ASCII control characters may be
// used, if they are available per user setup. Also, extended ASCII codes (>127), which are never in
// GCode programs, maybe selected for interface programs.
// NOTE: If changed, manually update help message in report.c.

// NOTE: All override realtime commands must be in the extended ASCII character set, starting
// at character value 128 (0x80) and up to 255 (0xFF). If the normal set of realtime commands,
// such as status reports, feed hold, reset, and cycle start, are moved to the extended set
// space, serial.c's RX ISR will need to be modified to accommodate the change.

enum class Cmd : uint8_t {
    Reset                 = 0x18,  // Ctrl-X
    StatusReport          = '?',
    CycleStart            = '~',
    FeedHold              = '!',
    SafetyDoor            = 0x84,
    JogCancel             = 0x85,
    DebugReport           = 0x86,  // Only when DEBUG_REPORT_REALTIME enabled, sends debug report in '{}' braces.
    FeedOvrReset          = 0x90,  // Restores feed override value to 100%.
    FeedOvrCoarsePlus     = 0x91,
    FeedOvrCoarseMinus    = 0x92,
    FeedOvrFinePlus       = 0x93,
    FeedOvrFineMinus      = 0x94,
    RapidOvrReset         = 0x95,  // Restores rapid override value to 100%.
    RapidOvrMedium        = 0x96,
    RapidOvrLow           = 0x97,
    RapidOvrExtraLow      = 0x98,  // *NOT SUPPORTED*
    SpindleOvrReset       = 0x99,  // Restores spindle override value to 100%.
    SpindleOvrCoarsePlus  = 0x9A,  // 154
    SpindleOvrCoarseMinus = 0x9B,
    SpindleOvrFinePlus    = 0x9C,
    SpindleOvrFineMinus   = 0x9D,
    SpindleOvrStop        = 0x9E,
    CoolantFloodOvrToggle = 0xA0,
    CoolantMistOvrToggle  = 0xA1,
#ifdef DEBUG_STEPPING
    TestPl = '^',
    TestSt = '%',
#endif
};

bool is_realtime_command(uint8_t data);
void execute_realtime_command(Cmd command, Channel& channel);

Channel* pollChannels(char* line = nullptr);

class AllChannels : public Channel {
    std::vector<Channel*> _channelq;

public:
    AllChannels() : Channel("all") {}

    void registration(Channel* channel);
    void deregistration(Channel* channel);
    void init();

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;

    // Stub implementations to satisfy Stream requirements
    int  available() override { return 0; }
    int  read() { return -1; }
    int  peek() { return -1; }
    void flush() {}

    // All channels cannot be a direct command source so
    // its rx_buffer_available() method should not be called.
    int rx_buffer_available() override { return 0; }

    String info();

    Channel* pollLine(char* line) override;
};

extern AllChannels allChannels;
