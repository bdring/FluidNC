// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Serial.cpp - Header for system level commands and real-time processes

  Original Grbl only supports communication via serial port. That is why this
  file is call serial.cpp. FluidNC supports many "clients".

  Clients are sources of commands like the serial port or a bluetooth connection.
  Multiple clients can be active at a time. If a client asks for status, only the client will
  receive the reply to the command.

  The serial port acts as the debugging port because it is always on and does not
  need to be reconnected after reboot. Messages about the configuration and other events
  are sent to the serial port automatically, without a request command. These are in the
  [MSG: xxxxxx] format which is part of the Grbl protocol.

  Important: It is up to the user that the clients play well together. Ideally, if one client
  is sending the gcode, the others should just be doing status, feedhold, etc.

  Clients send line-oriented command (GCode, $$, [ESP...], etc) and realtime commands (?,!.~, etc)
  A line-oriented command is a string of printable characters followed by a '\r' or '\n'
  A realtime commands is a single character with no '\r' or '\n'

  After sending a line-oriented command, you must wait for an OK to send another.
  This is because only a certain number of commands can be buffered at a time.
  The system will tell you when it is ready for another one with the OK.

  Realtime commands can be sent at any time and will acted upon very quickly.
  Realtime commands can be anywhere in the stream.

  To allow the realtime commands to be randomly mixed in the stream of data, we
  read all clients as fast as possible. The realtime commands are acted upon and the other characters are
  placed into a client_buffer[client].

  The main protocol loop reads from client_buffer[]
*/

#include "Serial.h"
#include "Uart.h"
#include "Machine/MachineConfig.h"
#include "WebUI/InputBuffer.h"
#include "WebUI/TelnetServer.h"
#include "WebUI/Serial2Socket.h"
#include "WebUI/Commands.h"
#include "WebUI/WifiConfig.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // rtSafetyDoor etc
#include "SDCard.h"

#include <atomic>
#include <cstring>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t clientCheckTaskHandle = 0;

WebUI::InputBuffer client_buffer[CLIENT_COUNT];  // create a buffer for each client

// Returns the number of bytes available in a client buffer.
int client_get_rx_buffer_available(client_t client) {
    switch (client) {
        case CLIENT_SERIAL:
            return 128 - Uart0.available();
        case CLIENT_BT:
            // XXX possibly wrong
            return 512 - WebUI::SerialBT.available();
        case CLIENT_TELNET:
            return WebUI::telnet_server.get_rx_buffer_available();
        default:
            return 64;
    }
}

void heapCheckTask(void* pvParameters) {
    static uint32_t heapSize = 0;
    while (true) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings and whatnot
        uint32_t newHeapSize = xPortGetFreeHeapSize();
        if (newHeapSize != heapSize) {
            heapSize = newHeapSize;
            log_info("heap " << heapSize);
        }
        vTaskDelay(3000 / portTICK_RATE_MS);  // Yield to other tasks

        static UBaseType_t uxHighWaterMark = 0;
#ifdef DEBUG_TASK_STACK
        reportTaskStackSize(uxHighWaterMark);
#endif
    }
}

void client_init() {
#ifdef DEBUG_REPORT_HEAP_SIZE
    // For a 2000-word stack, uxTaskGetStackHighWaterMark reports 288 words available
    xTaskCreatePinnedToCore(heapCheckTask, "heapTask", 2000, NULL, 1, NULL, 1);
#endif

    client_reset_read_buffer(CLIENT_ALL);
    clientCheckTaskHandle = 0;

    // create a task to check for incoming data
    // For a 4096-word stack, uxTaskGetStackHighWaterMark reports 244 words available
    // after WebUI attaches.
    xTaskCreatePinnedToCore(clientCheckTask,    // task
                            "clientCheckTask",  // name for task
                            8192,               // size of task stack
                            NULL,               // parameters
                            1,                  // priority
                            &clientCheckTaskHandle,
                            CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
    );
}

int AllClients::read() {
    for (size_t i = 0; i < CLIENT_ALL; i++) {
        int c = clients[i]->read();
        if (c >= 0) {
            // _lastReadClient = clients[i];
            _lastReadClient = static_cast<ClientType>(i);
            return c;
        }
    }
    return -1;
};
int AllClients::available() {
    for (size_t i = 0; i < CLIENT_ALL; i++) {
        int n = clients[i]->available();
        if (n > 0) {
            // _lastReadClient = clients[i];
            _lastReadClient = static_cast<ClientType>(i);
            return n;
        }
    }
    return 0;
};

size_t AllClients::write(uint8_t data) {
    for (size_t i = 0; i < CLIENT_ALL; i++) {
        clients[i]->write(data);
    }
    return 1;
};
size_t AllClients::write(const uint8_t* buffer, size_t length) {
    for (size_t i = 0; i < CLIENT_ALL; i++) {
        clients[i]->write(buffer, length);
    }
    return length;
};
void AllClients::flush() {
    for (size_t i = 0; i < CLIENT_ALL; i++) {
        clients[i]->flush();
    }
}

AllClients allClients;
Print*     allOut = static_cast<Print*>(&allClients);

Stream* clients[] = {
    static_cast<Stream*>(&Uart0),
    static_cast<Stream*>(&WebUI::SerialBT),
    static_cast<Stream*>(&WebUI::Serial2Socket),
    static_cast<Stream*>(&WebUI::telnet_server),
    static_cast<Stream*>(&WebUI::inputBuffer),
    static_cast<Stream*>(&allClients),
};

static ClientType getClientChar(int& data) {
    data = allClients.read();
    if (data >= 0) {
        return allClients.getLastClient();
    }
    return CLIENT_ALL;
}

// this task runs and checks for data on all interfaces
// Realtime stuff is acted upon, then characters are added to the appropriate buffer
void clientCheckTask(void* pvParameters) {
    int        data = 0;                                                    // Must be int so -1 value is possible
    ClientType client;                                                      // who sent the data
    while (true) {                                                          // run continuously
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // read fence for settings
        while ((client = getClientChar(data)) != CLIENT_ALL) {
            uint8_t clientByte = uint8_t(data);
            // Pick off realtime command characters directly from the serial stream. These characters are
            // not passed into the main buffer, but these set system state flag bits for realtime execution.
            if (is_realtime_command(clientByte)) {
                execute_realtime_command(static_cast<Cmd>(clientByte), client);
            } else {
                if (config->_sdCard->get_state() < SDCard::State::Busy) {
                    vTaskEnterCritical(&myMutex);
                    client_buffer[client].push(clientByte);
                    vTaskExitCritical(&myMutex);
                } else {
                    if (clientByte == '\r' || clientByte == '\n') {
                        _sendf(client, "error %d\r\n", Error::AnotherInterfaceBusy);
                        log_error("SD card job running");
                    }
                }
            }
        }  // if something available
        WebUI::COMMANDS::handle();

        if (config->_comms->_bluetoothConfig) {
            config->_comms->_bluetoothConfig->handle();
        }

        WebUI::wifi_config.handle();
        WebUI::Serial2Socket.handle_flush();

        vTaskDelay(1 / portTICK_RATE_MS);  // Yield to other tasks

#ifdef DEBUG_TASK_STACK
        static UBaseType_t uxHighWaterMark = 0;
        reportTaskStackSize(uxHighWaterMark);
#endif
    }
}

void client_reset_read_buffer(client_t client) {
    for (client_t client_num = 0; client_num < CLIENT_COUNT; client_num++) {
        if (client == client_num || client == CLIENT_ALL) {
            client_buffer[client_num].begin();
        }
    }
}

// Fetches the first byte in the client read buffer. Called by protocol loop.
int client_read(client_t client) {
    vTaskEnterCritical(&myMutex);
    int data = client_buffer[client].read();
    vTaskExitCritical(&myMutex);
    return data;
}

// checks to see if a character is a realtime character
bool is_realtime_command(uint8_t data) {
    if (data >= 0x80) {
        return true;
    }
    auto cmd = static_cast<Cmd>(data);
    return cmd == Cmd::Reset || cmd == Cmd::StatusReport || cmd == Cmd::CycleStart || cmd == Cmd::FeedHold;
}

// Act upon a realtime character
void execute_realtime_command(Cmd command, client_t client) {
    switch (command) {
        case Cmd::Reset:
            log_debug("Cmd::Reset");
            mc_reset();  // Call motion control reset routine.
            break;
        case Cmd::StatusReport:
            report_realtime_status(client);  // direct call instead of setting flag
            break;
        case Cmd::CycleStart:
            rtCycleStart = true;
            break;
        case Cmd::FeedHold:
            rtFeedHold = true;
            break;
        case Cmd::SafetyDoor:
            rtSafetyDoor = true;
            break;
        case Cmd::JogCancel:
            if (sys.state == State::Jog) {  // Block all other states from invoking motion cancel.
                rtMotionCancel = true;
            }
            break;
        case Cmd::DebugReport:
#ifdef DEBUG_REPORT_REALTIME
            rtExecDebug = true;
#endif
            break;
        case Cmd::SpindleOvrStop:
            rtAccessoryOverride.bit.spindleOvrStop = 1;
            break;
        case Cmd::FeedOvrReset:
            rtFOverride = FeedOverride::Default;
            break;
        case Cmd::FeedOvrCoarsePlus:
            rtFOverride += FeedOverride::CoarseIncrement;
            if (rtFOverride > FeedOverride::Max) {
                rtFOverride = FeedOverride::Max;
            }
            break;
        case Cmd::FeedOvrCoarseMinus:
            rtFOverride -= FeedOverride::CoarseIncrement;
            if (rtFOverride < FeedOverride::Min) {
                rtFOverride = FeedOverride::Min;
            }
            break;
        case Cmd::FeedOvrFinePlus:
            rtFOverride += FeedOverride::FineIncrement;
            if (rtFOverride > FeedOverride::Max) {
                rtFOverride = FeedOverride::Max;
            }
            break;
        case Cmd::FeedOvrFineMinus:
            rtFOverride -= FeedOverride::FineIncrement;
            if (rtFOverride < FeedOverride::Min) {
                rtFOverride = FeedOverride::Min;
            }
            break;
        case Cmd::RapidOvrReset:
            rtROverride = RapidOverride::Default;
            break;
        case Cmd::RapidOvrMedium:
            rtROverride = RapidOverride::Medium;
            break;
        case Cmd::RapidOvrLow:
            rtROverride = RapidOverride::Low;
            break;
        case Cmd::RapidOvrExtraLow:
            rtROverride = RapidOverride::ExtraLow;
            break;
        case Cmd::SpindleOvrReset:
            rtSOverride = SpindleSpeedOverride::Default;
            break;
        case Cmd::SpindleOvrCoarsePlus:
            rtSOverride += SpindleSpeedOverride::CoarseIncrement;
            if (rtSOverride > SpindleSpeedOverride::Max) {
                rtSOverride = SpindleSpeedOverride::Max;
            }
            break;
        case Cmd::SpindleOvrCoarseMinus:
            rtSOverride -= SpindleSpeedOverride::CoarseIncrement;
            if (rtSOverride < SpindleSpeedOverride::Min) {
                rtSOverride = SpindleSpeedOverride::Min;
            }
            break;
        case Cmd::SpindleOvrFinePlus:
            rtSOverride += SpindleSpeedOverride::FineIncrement;
            if (rtSOverride > SpindleSpeedOverride::Max) {
                rtSOverride = SpindleSpeedOverride::Max;
            }
            break;
        case Cmd::SpindleOvrFineMinus:
            rtSOverride -= SpindleSpeedOverride::FineIncrement;
            if (rtSOverride < SpindleSpeedOverride::Min) {
                rtSOverride = SpindleSpeedOverride::Min;
            }
            break;
        case Cmd::CoolantFloodOvrToggle:
            rtAccessoryOverride.bit.coolantFloodOvrToggle = 1;
            break;
        case Cmd::CoolantMistOvrToggle:
            rtAccessoryOverride.bit.coolantMistOvrToggle = 1;
            break;
    }
}

extern "C" {
#include <stdio.h>
}

static FILE* clientFile;

void client_write(client_t client, const char* text) {
    if (client == CLIENT_FILE) {
        size_t len    = strlen(text);
        size_t actual = fwrite(text, 1, len, clientFile);
        Assert(actual == len, "File write failed");
        return;
    }

    clients[client]->write((const uint8_t*)text, strlen(text));
}

void ClientStream::add(char c) {
    char text[2];
    text[1] = '\0';
    text[0] = c;
    client_write(_client, text);
}

ClientStream::ClientStream(const char* filename, const char* defaultFs) : _client(CLIENT_FILE) {
    String path;

    // Insert the default file system prefix if a file system name is not present
    if (*filename != '/') {
        path = "/";
        path += defaultFs;
        path += "/";
    }

    path += filename;

    // Map /localfs/ to the actual name of the local file system
    if (path.startsWith("/localfs/")) {
        path.replace("/localfs/", "/spiffs/");
    }
    if (path.startsWith("/sd/")) {
        if (config->_sdCard->begin(SDCard::State::BusyWriting) != SDCard::State::Idle) {
            throw Error::FsFailedMount;
        }
        _isSD = true;
    }

    clientFile = fopen(path.c_str(), "w");
    if (!clientFile) {
        throw Error::FsFailedCreateFile;
    }
}

ClientStream::~ClientStream() {
    if (_client == CLIENT_FILE) {
        fclose(clientFile);
    }
    if (_isSD) {
        config->_sdCard->end();
    }
}
