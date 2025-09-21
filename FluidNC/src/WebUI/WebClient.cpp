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
        //_xBufferLock = xSemaphoreCreateBinary();
        _background_task_handle = nullptr;
        xTaskCreatePinnedToCore(background_task,  // task
                                        "WebClient_background_task",             // name for task
                                        16384,                            // size of task stack
                                        this,                            // parameters
                                        1,                               // priority
                                        &_background_task_handle,
                                        SUPPORT_TASK_CORE  // core
        );
    }

    void WebClient::attachWS(AsyncWebServerRequest* request, bool silent) {
        _silent      = silent;
        _request   = request;
        _xBufferLock.lock();
        _buflen      = 0;
        _done=false;
        _xBufferLock.unlock();
        // size_t *buflen = &_buflen;
        // size_t *allocsize = &_allocsize;
        // char **src_buffer = &_buffer;
        // bool *done = &_done;
        // std::mutex *xBufferLock = &_xBufferLock;

        // _response = _request->beginChunkedResponse(
        //         "",
        //         [buflen, allocsize, src_buffer, done, xBufferLock](uint8_t *dest_buffer, size_t maxLen, size_t total) mutable -> size_t {
        //             //xSemaphoreTake(xBufferLock, portMAX_DELAY);
        //             xBufferLock->lock();
        //             if(buflen>0)
        //             {
        //                 int bytes = min(*buflen, maxLen); //min((int)min(*buflen-total, maxLen),1024);
        //                 char *b = *src_buffer;
        //                 Uart0.printf("Got buffer to send of size %d\n", bytes);
        //                 Uart0.flush();
        //                 memcpy(dest_buffer, b, bytes);
        //                 memmove(b, &b[bytes], *buflen-bytes);
        //                 *buflen-=bytes;
        //                 xBufferLock->unlock();
        //                 // if(total+bytes >= *buflen)
        //                 // {
        //                 //     free(*src_buffer);
        //                 //     *src_buffer=nullptr;
        //                 //     *allocsize=0;
        //                 //     *buflen=0;
        //                 // }

        //                 return bytes;
        //             }
        //             else if(*done)
        //             {
        //                 Uart0.printf("DONE\n");
        //                 Uart0.flush();
        //                 free(*src_buffer);
        //                 *src_buffer=nullptr;
        //                 *allocsize=0;
        //                 *buflen=0;
        //                 xBufferLock->unlock();
        //                 return 0;
        //             }
        //             else{
        //                 Uart0.printf("Waiting for data...\n");
        //                 Uart0.flush();
        //                 xBufferLock->unlock();
        //                 return RESPONSE_TRY_AGAIN;
        //             }
        //             xBufferLock->unlock();
        //             return 0;
        //         }
        //     );

        // _request->onDisconnect([buflen, allocsize, src_buffer, xBufferLock](){
        //     // TODO... ?
        //     Uart0.printf("WebClient on disconnect\n");
        //     xBufferLock->lock();
        //     if(*src_buffer){
        //         free(*src_buffer);
        //         *src_buffer=nullptr;
        //         *allocsize=0;
        //         *buflen=0;
        //     }
        //     xBufferLock->unlock();

        // });
        // _response->addHeader("Cache-Control", "no-cache");
        // _request->send(_response);

        
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
            Uart0.printf("DONE\n");
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
        // _xBufferLock.lock();
        // _done=true;
        // _xBufferLock.unlock();
        //xSemaphoreGive(_xBufferLock);
        // if(_buflen)
        // {
        //     size_t *buflen = &_buflen;
        //     size_t *allocsize = &_allocsize;
        //     char **src_buffer = &_buffer;
        //     _response = _request->beginResponse(
        //             "",
        //             _buflen,
        //             [buflen, allocsize, src_buffer](uint8_t *dest_buffer, size_t maxLen, size_t total) mutable -> size_t {
        //                 int bytes = min((int)min(*buflen-total, maxLen),1024);
        //                 char *b = *src_buffer;
        //                 memcpy(dest_buffer, &b[total], bytes);
                        
        //                 if(total+bytes >= *buflen)
        //                 {
        //                     free(*src_buffer);
        //                     *src_buffer=nullptr;
        //                     *allocsize=0;
        //                     *buflen=0;
        //                 }

        //                 return bytes;
        //             }
        //         );
        //     _response->addHeader("Cache-Control", "no-cache");
        //     _request->send(_response);
        // }
        // else
        // {
        //     _request->send(204,"","");
        // }
        // _request = nullptr;
        // _response = nullptr;
    }

    void WebClient::executeCommandBackground(const char *cmd){
        _xBufferLock.lock();
        _cmds.push_back(std::string(cmd));
        _xBufferLock.unlock();
    }

    void WebClient::background_task(void* pvParameters) {
        //AsyncWebServerRequest *request = static_cast<AsyncWebServerRequest*>(pvParameters);
        WebClient *_webClient = static_cast<WebClient*>(pvParameters);
        //Uart0.printf("New WebClient::background_task started\n"); // Crash loop, Uart0 may not be created?
        std::string cmd;
        for (; true; delay_ms(1)) {
        _webClient->_xBufferLock.lock();

            if(_webClient->_cmds.size() > 0){
                cmd = _webClient->_cmds.front();
                _webClient->_cmds.pop_front();
                _webClient->_xBufferLock.unlock(); 

                Uart0.printf("Got a new command to run in background...\n");
                
                settings_execute_line(cmd.c_str(), *_webClient, AuthenticationLevel::LEVEL_ADMIN);
                Uart0.printf("We got out of settings_execute_line\n");
                // Should not call detach, since we still need to send the remaining buffer, so we should not free and clear yet.
                _webClient->_done=true;

            }else{
               _webClient->_xBufferLock.unlock(); 
            }
        }
        Uart0.printf("New WebClient::background_task started\n");

    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (!_request || _silent) {
            return length;
        }
        _xBufferLock.lock();
        if(_done)
        {
            return length;
            _xBufferLock.unlock();
        }
        if(_buflen+length > _allocsize){
            if(_allocsize >= 4096){
                Uart0.printf("_allocsize is big, will wait\n");
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
                _allocsize = _allocsize + ((length / 2048 + 1)*2048);
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

    // Flush is no longer really possible, we can only send the response at the end
    // once, to the client. So we do the sending in the detach since multiple flush/send would not work.
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
        /*if (_request) {
            _request->send(code, "text/plain", line.c_str());
        }
        */
    }

    WebClient::~WebClient() {
        if(_fs)
            delete _fs;
        //vSemaphoreDelete(_xBufferLock);
    }
}
