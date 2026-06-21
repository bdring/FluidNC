// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Report.h"
#include "FileStream.h"
#include "WebClient.h"
#include "Driver/Console.h"
#include <ESPAsyncWebServer.h>
#include "Settings.h"        // settings_execute_line()
#include "Authentication.h"  // Auth levels

namespace WebUI {
    QueueHandle_t WebClients::_background_task_queue  = nullptr;
    TaskHandle_t  WebClients::_background_task_handle = nullptr;

    void WebClients::background_task(void* pvParameters) {
        std::string cmd;
        while (true) {
            WebClient* webClient;
            if (xQueueReceive(_background_task_queue, &webClient, portMAX_DELAY) == pdTRUE) {
                webClient->xBufferLock.lock();
                if (webClient->cmds.size() > 0) {
                    cmd = webClient->cmds.front();
                    webClient->cmds.pop_front();
                    webClient->xBufferLock.unlock();
                    // TODO: check error result and see if we can do anything...
                    settings_execute_line(cmd.c_str(), *webClient, AuthenticationLevel::LEVEL_ADMIN);
                    // Should not call detach, since we still need to send the remaining buffer, so we should not free and clear yet.
                    webClient->xBufferLock.lock();
                    webClient->done = true;
                    webClient->xBufferLock.unlock();
                } else {
                    webClient->xBufferLock.unlock();
                }
            } else
                delay(1);  // This should never happen if portMAX_DELAY is trully infinite
        }
    }

    WebClient::WebClient() : Channel("webclient") {
        if (WebClients::_background_task_queue == nullptr)  // If we are the first instanciation ever, create the event queue
            WebClients::_background_task_queue = xQueueCreate(64, sizeof(WebClient*));
        if (WebClients::_background_task_handle == nullptr) {     // Same here, create the unique background task
            xTaskCreatePinnedToCore(WebClients::background_task,  // task
                                    "WebClient_background_task",  // name for task
                                    5 * 1024,                     // 4KB seems enough, 3.5 crash, setting to 5KB
                                    NULL,                         // parameters
                                    20,  // priority // If higher than (ASYNC TCP?) the commands like [ESP800] return faster
                                    &WebClients::_background_task_handle,
                                    SUPPORT_TASK_CORE  // not sure if SUPPORT_TASK_CORE is the best choice
            );
        }
    }

    WebClient::~WebClient() {
        xBufferLock.lock();
        if (_buffer)
            free(_buffer);
        _buffer    = nullptr;
        _allocsize = 0;
        _buflen    = 0;
        done       = true;
        xBufferLock.unlock();
    }

    void WebClient::attachWS(bool silent) {
        _silent = silent;
        xBufferLock.lock();
        _buflen    = 0;
        _allocsize = 0;
        if (_buffer) {
            free(_buffer);
            _buffer = nullptr;
        }
        done = false;
        xBufferLock.unlock();
    }

    // Should be used externally to signify to free any potential resources
    // Any unread buffer will be cleared after that.
    void WebClient::detachWS() {
        xBufferLock.lock();
        _silent = true;
        while (!done) {
            //done = true;
            xBufferLock.unlock();
            delay(1);
            xBufferLock.lock();
        }
        xBufferLock.unlock();
    }

    size_t WebClient::copyBufferSafe(uint8_t* dest_buffer, size_t maxLen, size_t total) {
        xBufferLock.lock();
        if (_buflen > 0) {
            int bytes = min(_buflen, maxLen);
            memcpy(dest_buffer, _buffer, bytes);
            memmove(_buffer, &_buffer[bytes], _buflen - bytes);
            _buflen -= bytes;
            xBufferLock.unlock();
            return bytes;
        } else if (done) {
            free(_buffer);
            _buffer    = nullptr;
            _allocsize = 0;
            _buflen    = 0;
            xBufferLock.unlock();
            return 0;
        }
        xBufferLock.unlock();
        return RESPONSE_TRY_AGAIN;
    }

    void WebClient::executeCommandBackground(const char* cmd) {
        xBufferLock.lock();
        cmds.push_back(std::string(cmd));
        WebClient* _this = this;
        xQueueSend(WebClients::_background_task_queue, &_this, portTICK_PERIOD_MS * 100);
        xBufferLock.unlock();
    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent || !_active) {
            return length;
        }
        xBufferLock.lock();
        if (_buflen + length > _allocsize) {
            if (_allocsize >= BUFLEN) {
                while (_buflen + length > _allocsize && !_silent && _active) {
                    xBufferLock.unlock();
                    delay(1);
                    xBufferLock.lock();
                }
                if (_silent || !_active) {
                    xBufferLock.unlock();
                    return length;
                }
            } else {
                _allocsize       = _allocsize + ((length / 256 + 1) * 256);
                char* new_buffer = (char*)realloc((void*)_buffer, _allocsize);
                if (!new_buffer) {
                    log_info_to(Console, "Not enough memory!" << _allocsize);
                    xBufferLock.unlock();
                    return length;
                }
                _buffer = new_buffer;
            }
        }
        if (_buffer) {
            memcpy(&_buffer[_buflen], buffer, length);
            _buflen += length;
        }
        xBufferLock.unlock();

        return length;
    }

    size_t WebClient::write(uint8_t data) {
        return write(&data, 1);
    }

    // Flush is no longer really needed
    void WebClient::flush() {}

    void WebClient::sendLine(MsgLevel level, const char* line) {
        print_msg(level, line);
    }
    void WebClient::sendLine(MsgLevel level, const std::string* line) {
        print_msg(level, line->c_str());
        delete line;
    }
    void WebClient::sendLine(MsgLevel level, const std::string& line) {
        print_msg(level, line.c_str());
    }

    void WebClient::out(const char* s, const char* tag) {
        write((uint8_t*)s, strlen(s));
    }

    void WebClient::out(const std::string& s, const char* tag) {
        write((uint8_t*)s.c_str(), s.size());
    }

    void WebClient::out_acked(const std::string& s, const char* tag) {
        out(s, tag);
    }

    void WebClient::sendError(uint16_t code, const std::string& line) {}
}
