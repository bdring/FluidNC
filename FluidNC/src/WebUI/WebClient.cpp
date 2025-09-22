// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Report.h"
#include "src/FileStream.h"
#include "WebClient.h"
#include "src/UartChannel.h" 
#include <ESPAsyncWebServer.h>
#include "src/Settings.h"  // settings_execute_line()
#include "Authentication.h"  // Auth levels

namespace WebUI {
    WebClient webClient;

    WebClient::WebClient() : Channel("webclient") {
        _background_task_handle = nullptr;
        xTaskCreatePinnedToCore(background_task,                        // task
                                        "WebClient_background_task",    // name for task
                                        16384,                          // size of task stack
                                        this,                           // parameters
                                        1,                              // priority
                                        &_background_task_handle,
                                        SUPPORT_TASK_CORE               // core
        );
    }

    void WebClient::attachWS(bool silent) {
        _silent      = silent;
        _xBufferLock.lock();
        _buflen      = 0;
        _allocsize   = 0;
        if(_buffer)
        {
            free(_buffer);
            _buffer=nullptr;
        }
        _done=false;
        _xBufferLock.unlock();
    }

    int WebClient::copyBufferSafe(uint8_t *dest_buffer, size_t maxLen, size_t total){
        _xBufferLock.lock();
        if(_buflen>0)
        {
            int bytes = min(_buflen, maxLen);
            memcpy(dest_buffer, _buffer, bytes);
            memmove(_buffer, &_buffer[bytes], _buflen-bytes);
            _buflen-=bytes;
            _xBufferLock.unlock();
            return bytes;
        }
        else if(_done)
        {
            free(_buffer);
            _buffer=nullptr;
            _allocsize=0;
            _buflen=0;
            _xBufferLock.unlock();
            return 0;
        }
        _xBufferLock.unlock();
        return RESPONSE_TRY_AGAIN;
    }

    // Should be used externally to signify to free any potential resources
    // Any unread buffer will be cleared after that.
    void WebClient::detachWS() {
        _xBufferLock.lock();
        if(_buffer)
            free(_buffer);
        _buffer=nullptr;
        _allocsize=0;
        _buflen=0;
        _done=true;
        _xBufferLock.unlock();
     }

    void WebClient::executeCommandBackground(const char *cmd){
        _xBufferLock.lock();
        _cmds.push_back(std::string(cmd));
        _xBufferLock.unlock();
    }

    void WebClient::background_task(void* pvParameters) {
        WebClient *_webClient = static_cast<WebClient*>(pvParameters);
        std::string cmd;
        for (; true; delay_ms(1)) {
        _webClient->_xBufferLock.lock();

            if(_webClient->_cmds.size() > 0){
                cmd = _webClient->_cmds.front();
                _webClient->_cmds.pop_front();
                _webClient->_xBufferLock.unlock(); 
                // TODO: check error result and see if we can do anything...
                settings_execute_line(cmd.c_str(), *_webClient, AuthenticationLevel::LEVEL_ADMIN);
                // Should not call detach, since we still need to send the remaining buffer, so we should not free and clear yet.
                _webClient->_done=true; // TODO Forgot to lock mutex...

            }else{
               _webClient->_xBufferLock.unlock(); 
            }
        }
        Uart0.printf("WebClient::background_task is exiting... should this happen?\n");

    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (_silent) {
            return length;
        }
        _xBufferLock.lock();
        if(_done)
        {
            return length;
            _xBufferLock.unlock();
        }
        if(_buflen+length > _allocsize){
            if(_allocsize >= BUFLEN){
                //Uart0.printf("_allocsize is big, will wait\n");
                while(_buflen+length > _allocsize && !_done){
                    _xBufferLock.unlock();
                    delay(1);
                    _xBufferLock.lock();
                }
                if(_done)
                {
                     _xBufferLock.unlock();
                    return length;
                }
            }
            else
            {
                _allocsize = _allocsize + ((length / 512 + 1)*512);
                char * new_buffer = (char*)realloc((void*)_buffer, _allocsize);
                if(!new_buffer){
                    log_info_to(Uart0, "Not enough memory!" << _allocsize);
                    _xBufferLock.unlock();
                    return length;
                }
                _buffer = new_buffer;
            }
        }
        if(_buffer)
        {
            memcpy(&_buffer[_buflen], buffer, length);
            _buflen+=length;
        }
        _xBufferLock.unlock();
        
        return length;
    }

    size_t WebClient::write(uint8_t data) {
        return write(&data, 1);
    }

    // Flush is no longer really needed
    void WebClient::flush() {
    }

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

    void WebClient::sendError(int code, const std::string& line) {
    }

    WebClient::~WebClient() {
    }
}
