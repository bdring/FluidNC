#pragma once

/*
  Serial.h - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdint.h"

// TODO: Change to enum class, and instead of 'uint8_t client' everywhere, change to ClientType client.
enum ClientType : uint8_t {
    CLIENT_SERIAL = 0,
    CLIENT_BT     = 1,
    CLIENT_WEBUI  = 2,
    CLIENT_TELNET = 3,
    CLIENT_INPUT  = 4,
    CLIENT_ALL    = 0xFF,
    CLIENT_COUNT  = 5,  // total number of client types regardless if they are used
    CLIENT_FILE   = 6,  // Not included in CLIENT_COUNT
};

// a task to read for incoming data from serial port
void clientCheckTask(void* pvParameters);

void client_write(uint8_t client, const char* text);

// Fetches the first byte in the serial read buffer. Called by main program.
int client_read(uint8_t client);

// See if the character is an action command like feedhold or jogging. If so, do the action and return true
uint8_t check_action_command(uint8_t data);

void client_init();
void client_reset_read_buffer(uint8_t client);

// Returns the number of bytes available in the RX serial buffer.
uint8_t client_get_rx_buffer_available(uint8_t client);

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

void execute_realtime_command(Cmd command, uint8_t client);
bool is_realtime_command(uint8_t data);

#include "SimpleOutputStream.h"

class ClientStream : public SimpleOutputStream {
    uint8_t _client;
    bool    _isSD;

public:
    ClientStream(uint8_t client) : _client(client) {}
    ClientStream(const char* filename, const char* defaultFs);

    void add(char c) override;
    ~ClientStream();
};
