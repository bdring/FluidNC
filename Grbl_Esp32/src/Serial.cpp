/*
  Serial.cpp - Header for system level commands and real-time processes
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modified for use on the ESP32
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

  What is going on here?

  Original Grbl only supports communication via serial port. That is why this
  file is call serial.cpp. Grbl_ESP32 supports many "clients".

  Clients are sources of commands like the serial port or a bluetooth connection.
  Multiple clients can be active at a time. If a client asks for status, only the client will
  receive the reply to the command.

  The serial port acts as the debugging port because it is always on and does not
  need to be reconnected after reboot. Messages about the configuration and other events
  are sent to the serial port automatically, without a request command. These are in the
  [MSG: xxxxxx] format. Gcode senders are should be OK with this because Grbl has always
  send some messages like this.

  Important: It is up user that the clients play well together. Ideally, if one client
  is sending the gcode, the others should just be doing status, feedhold, etc.

  Clients send gcode, grbl commands ($$, [ESP...], etc) and realtime commands (?,!.~, etc)
  Gcode and Grbl commands are a string of printable characters followed by a '\r' or '\n'
  Realtime commands are single characters with no '\r' or '\n'

  After sending a gcode or grbl command, you must wait for an OK to send another.
  This is because only a certain number of commands can be buffered at a time.
  Grbl will tell you when it is ready for another one with the OK.

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
uint8_t client_get_rx_buffer_available(uint8_t client) {
    return 128 - Uart0.available();
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

static ClientType getClientChar(int& data) {
    if (client_buffer[CLIENT_SERIAL].availableforwrite() && (data = Uart0.read()) != -1) {
        return CLIENT_SERIAL;
    }
    if (WebUI::inputBuffer.available()) {
        data = WebUI::inputBuffer.read();
        return CLIENT_INPUT;
    }

    if ((data = WebUI::SerialBT.read()) != -1) {
        return CLIENT_BT;
    }
    if ((data = WebUI::Serial2Socket.read()) != -1) {
        return CLIENT_WEBUI;
    }
    if ((data = WebUI::telnet_server.read()) != -1) {
        return CLIENT_TELNET;
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
                    client_buffer[client].write(clientByte);
                    vTaskExitCritical(&myMutex);
                } else {
                    if (clientByte == '\r' || clientByte == '\n') {
                        grbl_sendf(client, "error %d\r\n", Error::AnotherInterfaceBusy);
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

void client_reset_read_buffer(uint8_t client) {
    for (uint8_t client_num = 0; client_num < CLIENT_COUNT; client_num++) {
        if (client == client_num || client == CLIENT_ALL) {
            client_buffer[client_num].begin();
        }
    }
}

// Fetches the first byte in the client read buffer. Called by protocol loop.
int client_read(uint8_t client) {
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
void execute_realtime_command(Cmd command, uint8_t client) {
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

void client_write(uint8_t client, const char* text) {
    if (client == CLIENT_INPUT) {
        return;
    }

    WebUI::SerialBT.print(text);
    if (client == CLIENT_WEBUI || client == CLIENT_ALL) {
        WebUI::Serial2Socket.write((const uint8_t*)text, strlen(text));
    }
    if (client == CLIENT_TELNET || client == CLIENT_ALL) {
        WebUI::telnet_server.write((const uint8_t*)text, strlen(text));
    }

    if (client == CLIENT_SERIAL || client == CLIENT_ALL) {
        // This used to be Serial.write(text) before we made the Uart class
        // The Arduino HardwareSerial class is buggy in some versions.
        Uart0.write(text);
    }

    if (client == CLIENT_FILE) {
        size_t len    = strlen(text);
        size_t actual = fwrite(text, 1, len, clientFile);
        Assert(actual == len, "File write failed");
    }
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
