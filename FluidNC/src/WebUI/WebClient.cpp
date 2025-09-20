// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Report.h"
#include "src/FileStream.h"
#include "WebClient.h"
#include <ESPAsyncWebServer.h>


namespace WebUI {
    WebClient webClient;

    WebClient::WebClient() : Channel("webclient") {}

    void WebClient::attachWS(AsyncWebServerRequest* request, bool silent) {
        _silent      = silent;
        _request   = request;
        _buflen      = 0;
    }

    void WebClient::detachWS() {

        if(_buflen)
        {
            size_t *buflen = &_buflen;
            size_t *allocsize = &_allocsize;
            char **src_buffer = &_buffer;
            _response = _request->beginResponse(
                    "",
                    _buflen,
                    [buflen, allocsize, src_buffer](uint8_t *dest_buffer, size_t maxLen, size_t total) mutable -> size_t {
                        int bytes = min((int)min(*buflen-total, maxLen),1000);
                        char *b = *src_buffer;
                        memcpy(dest_buffer, &b[total], bytes);
                        
                        if(total+bytes >= *buflen)
                        {
                            free(*src_buffer);
                            *src_buffer=nullptr;
                            *allocsize=0;
                            *buflen=0;
                        }

                        return bytes;
                    }
                );
            _response->addHeader("Cache-Control", "no-cache");
            _request->send(_response);
        }
        else
        {
            _request->send(204,"","");
        }
        _request = nullptr;
        _response = nullptr;
    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (!_request || _silent) {
            return length;
        }

        if(_buflen+length > _allocsize){
            _allocsize = _allocsize + ((length / 2048 + 1)*2048);
            char * new_buffer = (char*)realloc((void*)_buffer, _allocsize);
            if(!new_buffer){
                log_info("Not enough memory!");
                return 0;
            }
            _buffer = new_buffer;
        }
        if(_buffer)
        {
            memcpy(&_buffer[_buflen], buffer, length);
            _buflen+=length;
        }
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
        if (_request) {
            _request->send(code, "text/plain", line.c_str());
        }
    }

    WebClient::~WebClient() {
        if(_fs)
            delete _fs;
    }
}
