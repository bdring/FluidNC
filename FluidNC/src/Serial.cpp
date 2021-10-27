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

  After sending a line-oriented command, a sender must wait for an OK to send another.
  This is because only a certain number of commands can be buffered at a time.
  The system will tell you when it is ready for another one with the OK.

  Realtime commands can be sent at any time and will acted upon very quickly.
  Realtime commands can be anywhere in the stream.

  To allow the realtime commands to be randomly mixed in the stream of data, we
  read all clients as fast as possible. The realtime commands are acted upon and
  the other characters are placed into a per-client buffer.  When a complete line
  is received, pollClient returns the associated client spec.
*/

#include "Serial.h"
#include "Uart.h"
#include "Machine/MachineConfig.h"
#include "WebUI/InputBuffer.h"
#include "WebUI/Commands.h"
#include "WebUI/WifiServices.h"
#include "MotionControl.h"
#include "Report.h"
#include "System.h"
#include "Protocol.h"  // rtSafetyDoor etc
#include "SDCard.h"
#include "WebUI/InputBuffer.h"  // XXX could this be a StringStream ?
#include "Main.h"               // display()

#include <atomic>
#include <cstring>
#include <vector>
#include <freertos/task.h>  // portMUX_TYPE, TaskHandle_T

portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t clientCheckTaskHandle = 0;

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

// Act upon a realtime character
static void execute_realtime_command(Cmd command, Print& client) {
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

// checks to see if a character is a realtime character
bool is_realtime_command(uint8_t data) {
    if (data >= 0x80) {
        return true;
    }
    auto cmd = static_cast<Cmd>(data);
    return cmd == Cmd::Reset || cmd == Cmd::StatusReport || cmd == Cmd::CycleStart || cmd == Cmd::FeedHold;
}

InputClient* sdClient = new InputClient(nullptr);

std::vector<InputClient*> clientq;

void register_client(Stream* client_stream) {
    clientq.push_back(new InputClient(client_stream));
}
void client_init() {
    register_client(&Uart0);               // USB Serial
    register_client(&WebUI::inputBuffer);  // Macros
}
InputClient* pollClients() {
    auto sdcard = config->_sdCard;

    for (auto client : clientq) {
        auto source = client->_in;
        int  c      = source->read();

        if (c >= 0) {
            char ch = c;
            if (client->_line_returned) {
                client->_line_returned = false;
                client->_linelen       = 0;
            }
            if (is_realtime_command(c)) {
                execute_realtime_command(static_cast<Cmd>(c), *source);
                continue;
            }

            if (ch == '\b') {
                // Simple editing for interactive input - backspace erases
                if (client->_linelen) {
                    --client->_linelen;
                }
                continue;
            }
            if ((client->_linelen + 1) == InputClient::maxLine) {
                report_status_message(Error::Overflow, *source);
                // XXX could show a message with the overflow line
                // XXX There is a problem here - the final fragment of the
                // too-long line will be returned as a valid line.  We really
                // need to enter a state where we ignore characters until
                // the newline.
                client->_linelen = 0;
                continue;
            }
            if (ch == '\r') {
                continue;
            }
            if (ch == '\n') {
                client->_line_num++;
                if (sdcard->get_state() < SDCard::State::Busy) {
                    client->_line[client->_linelen] = '\0';
                    client->_line_returned          = true;
                    display("GCODE", client->_line);
                    return client;
                } else {
                    // Log an error and discard the line if it happens during an SD run
                    log_error("SD card job running");
                    client->_linelen = 0;
                    continue;
                }
            }

            client->_line[client->_linelen] = ch;
            ++client->_linelen;
        }
    }

    WebUI::COMMANDS::handle();  // Handles feeding watchdog and ESP restart
#ifdef ENABLE_WIFI
    WebUI::wifi_services.handle();  // OTA, web_server, telnet_server polling
#endif

    // _readyNext indicates that input is coming from a file and
    // the GCode system is ready for another line.
    if (sdcard && sdcard->_readyNext) {
        Error res = sdcard->readFileLine(sdClient->_line, InputClient::maxLine);
        if (res == Error::Ok) {
            sdClient->_out     = &sdcard->getClient();
            sdcard->_readyNext = false;
            return sdClient;
        }
        report_status_message(res, sdcard->getClient());
    }

    return nullptr;
}

size_t AllClients::write(uint8_t data) {
    for (auto client : clientq) {
        client->_out->write(data);
    }
    return 1;
}
size_t AllClients::write(const uint8_t* buffer, size_t length) {
    for (auto client : clientq) {
        client->_out->write(buffer, length);
    }
    return length;
}

AllClients allClients;
