// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

/*
  Serial.h - Header for system level commands and real-time processes
*/

#include "stdint.h"

typedef uint8_t client_t;

// TODO: Change to enum class, and instead of 'uint8_t client' everywhere, change to ClientType client.
enum ClientType : client_t {
    CLIENT_SERIAL = 0,
    CLIENT_BT     = 1,
    CLIENT_WEBUI  = 2,
    CLIENT_TELNET = 3,
    CLIENT_INPUT  = 4,
    CLIENT_ALL    = 5,
    CLIENT_COUNT  = 5,  // total number of client types regardless if they are used
    CLIENT_FILE   = 6,  // Not included in CLIENT_COUNT
};

// a task to read for incoming data from serial port
void clientCheckTask(void* pvParameters);

void client_write(client_t client, const char* text);

// Fetches the first byte in the serial read buffer. Called by main program.
int client_read(client_t client);

// See if the character is an action command like feedhold or jogging. If so, do the action and return true
uint8_t check_action_command(uint8_t data);

void client_init();
void client_reset_read_buffer(client_t client);

// Returns the number of bytes available in the RX serial buffer.
int client_get_rx_buffer_available(client_t client);

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
};

void execute_realtime_command(Cmd command, client_t client);
bool is_realtime_command(uint8_t data);

#include "SimpleOutputStream.h"

class ClientStream : public SimpleOutputStream {
    client_t _client;
    bool     _isSD;

public:
    ClientStream(client_t client) : _client(client) {}
    ClientStream(const char* filename, const char* defaultFs);

    void add(char c) override;
    ~ClientStream();
};

#include <Stream.h>
class AllClients : public Stream {
    // Stream* _lastReadClient;
    ClientType _lastReadClient;

public:
    AllClients() = default;
    int read() override;
    int available() override;
    int peek() override { return -1; }

    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;
    void   flush() override;

    // Stream* getLastClient() { return _lastReadClient; }
    ClientType getLastClient() { return _lastReadClient; }
};

extern Stream*    clients[];
extern AllClients allClients;
