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
    namespace {
        constexpr UBaseType_t webclient_task_priority() {
#ifdef configMAX_PRIORITIES
            return (configMAX_PRIORITIES > 1) ? ((20 < (configMAX_PRIORITIES - 1)) ? 20 : (configMAX_PRIORITIES - 1)) : 0;
#else
            return 20;
#endif
        }
    }

    QueueHandle_t WebClients::_background_task_queue  = nullptr;
    TaskHandle_t  WebClients::_background_task_handle = nullptr;

    void WebClients::background_task(void* pvParameters) {
        std::string cmd;
        while (true) {
            WebClient* webClient;
            if (xQueueReceive(_background_task_queue, &webClient, portMAX_DELAY) == pdTRUE) {
                xSemaphoreTake(webClient->xBufferLock, portMAX_DELAY);
                if (webClient->cmds.size() > 0) {
                    cmd = webClient->cmds.front();
                    webClient->cmds.pop_front();
                    xSemaphoreGive(webClient->xBufferLock);
                    // TODO: check error result and see if we can do anything...
                    settings_execute_line(cmd.c_str(), *webClient, AuthenticationLevel::LEVEL_ADMIN);
                    // Should not call detach, since we still need to send the remaining buffer, so we should not free and clear yet.
                    xSemaphoreTake(webClient->xBufferLock, portMAX_DELAY);
                    webClient->done = true;
                    xSemaphoreGive(webClient->xBufferLock);
                } else {
                    xSemaphoreGive(webClient->xBufferLock);
                }
            } else
                delay(1);  // This should never happen if portMAX_DELAY is trully infinite
        }
    }

    WebClient::WebClient() : Channel("webclient") {
        xBufferLock = xSemaphoreCreateMutex();
        if (WebClients::_background_task_queue == nullptr) {  // If we are the first instanciation ever, create the event queue
            WebClients::_background_task_queue = xQueueCreate(64, sizeof(WebClient*));
        }
        if (WebClients::_background_task_handle == nullptr) {  // Same here, create the unique background task
#if defined(PICO_RP2040) || defined(PICO_RP2350)
            xTaskCreateAffinitySet(WebClients::background_task,  // task
                                   "WebClient_background_task",  // name for task
                                   5 * 1024,                     // 4KB seems enough, 3.5 crash, setting to 5KB
                                   NULL,                         // parameters
                                   webclient_task_priority(),  // Keep within configMAX_PRIORITIES
                                   (1 << SUPPORT_TASK_CORE),  // affinity mask
                                   &WebClients::_background_task_handle);
#else
            xTaskCreatePinnedToCore(WebClients::background_task,
                                    "WebClient_background_task",
                                    5 * 1024,
                                    NULL,
                                    webclient_task_priority(),
                                    &WebClients::_background_task_handle,
                                    SUPPORT_TASK_CORE);
#endif
        }
    }

    WebClient::~WebClient() {
        if (!xBufferLock) {
            return;
        }
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        if (_buffer)
            free(_buffer);
        _buffer    = nullptr;
        _allocsize = 0;
        _buflen    = 0;
        done       = true;
        xSemaphoreGive(xBufferLock);
        vSemaphoreDelete(xBufferLock);
        xBufferLock = nullptr;
    }

    void WebClient::attachWS(bool silent) {
        _silent = silent;
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        _buflen    = 0;
        _allocsize = 0;
        if (_buffer) {
            free(_buffer);
            _buffer = nullptr;
        }
        done = false;
        xSemaphoreGive(xBufferLock);
    }

    // Should be used externally to signify to free any potential resources
    // Any unread buffer will be cleared after that.
    void WebClient::detachWS() {
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        _silent = true;
        while (!done) {
            //done = true;
            xSemaphoreGive(xBufferLock);
            delay(1);
            xSemaphoreTake(xBufferLock, portMAX_DELAY);
        }
        xSemaphoreGive(xBufferLock);
    }

    size_t WebClient::copyBufferSafe(uint8_t* dest_buffer, size_t maxLen, size_t total) {
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        if (_buflen > 0) {
            int bytes = min(_buflen, maxLen);
            memcpy(dest_buffer, _buffer, bytes);
            memmove(_buffer, &_buffer[bytes], _buflen - bytes);
            _buflen -= bytes;
            xSemaphoreGive(xBufferLock);
            return bytes;
        } else if (done) {
            free(_buffer);
            _buffer    = nullptr;
            _allocsize = 0;
            _buflen    = 0;
            xSemaphoreGive(xBufferLock);
            return 0;
        }

        // Give the background command a brief chance to produce output so
        // chunked HTTP responses do not spin on empty buffers unnecessarily.
        size_t wait_loops = 0;
        while (_buflen == 0 && !done && !_silent && _active && wait_loops < 20) {
            xSemaphoreGive(xBufferLock);
            delay(1);
            ++wait_loops;
            xSemaphoreTake(xBufferLock, portMAX_DELAY);
            if (_buflen > 0) {
                int bytes = min(_buflen, maxLen);
                memcpy(dest_buffer, _buffer, bytes);
                memmove(_buffer, &_buffer[bytes], _buflen - bytes);
                _buflen -= bytes;
                xSemaphoreGive(xBufferLock);
                return bytes;
            }
        }

        if (done) {
            free(_buffer);
            _buffer    = nullptr;
            _allocsize = 0;
            _buflen    = 0;
            xSemaphoreGive(xBufferLock);
            return 0;
        }

        xSemaphoreGive(xBufferLock);
        return RESPONSE_TRY_AGAIN;
    }

    void WebClient::executeCommandBackground(const char* cmd) {
        if (!WebClients::_background_task_queue || !WebClients::_background_task_handle || !xBufferLock) {
            settings_execute_line(cmd, *this, AuthenticationLevel::LEVEL_ADMIN);
            done = true;
            return;
        }
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        cmds.push_back(std::string(cmd));
        WebClient* _this = this;
        xQueueSend(WebClients::_background_task_queue, &_this, portTICK_PERIOD_MS * 100);
        xSemaphoreGive(xBufferLock);
    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent || !_active) {
            return length;
        }
        xSemaphoreTake(xBufferLock, portMAX_DELAY);
        if (_buflen + length > _allocsize) {
            if (_allocsize >= BUFLEN) {
                while (_buflen + length > _allocsize && !_silent && _active) {
                    xSemaphoreGive(xBufferLock);
                    delay(1);
                    xSemaphoreTake(xBufferLock, portMAX_DELAY);
                }
                if (_silent || !_active) {
                    xSemaphoreGive(xBufferLock);
                    return length;
                }
            } else {
                _allocsize       = _allocsize + ((length / 256 + 1) * 256);
                char* new_buffer = (char*)realloc((void*)_buffer, _allocsize);
                if (!new_buffer) {
                    log_info_to(Console, "Not enough memory!" << _allocsize);
                    xSemaphoreGive(xBufferLock);
                    return length;
                }
                _buffer = new_buffer;
            }
        }
        if (_buffer) {
            memcpy(&_buffer[_buflen], buffer, length);
            _buflen += length;
        }
        xSemaphoreGive(xBufferLock);
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
