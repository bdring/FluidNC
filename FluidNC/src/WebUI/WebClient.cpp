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
        _header_sent = false;
        _silent      = silent;
        _request   = request;
        _buflen      = 0;
        if(_fs){
            delete _fs;
            _fs = nullptr;
        }
        _fs = new FileStream("/_webconfig.json", "w");
        //_response = _request->beginResponseStream("text/html");
    }

    void WebClient::detachWS() {
       // flush();
        //_response = _request->beginResponseStream("text/html");
       // log_info("Buffer len: " << _buflen);
        //_fs->set_position(0);
        if(_fs)
        {
            delete _fs;
            _fs = new FileStream("/_webconfig.json", "r");
            FileStream *fs = _fs;
            _response = _request->beginResponse(
                    "",
                    _fs->size(),
                    [fs](uint8_t *buffer, size_t maxLen, size_t total) mutable -> size_t {
                        if(total >= fs->size())
                            return 0;
                        // Seek on each chunk, to avoid concurrent client to get the wrong data, while still reusing the same filestream
                        fs->set_position(total);
                        int bytes = fs->read(buffer, maxLen);

                        return max(0, bytes); // return 0 even when no bytes were loaded
                    }
                );
            _response->addHeader("Cache-Control", "no-cache");
            _request->send(_response);
            _request = nullptr;
            _response = nullptr;
        }
        else
            _request->send(500,"rext/plain","Error");
        
    }

    size_t WebClient::write(const uint8_t* buffer, size_t length) {
        if (!_request || _silent) {
            return length;
        }
        if (!_header_sent) {
            //_request->setContentLength(CONTENT_LENGTH_UNKNOWN);
            // The webserver code automatically sends Content-Type: text/html
            // so no need to do it explicitly
            // _webserver->sendHeader("Content-Type", "text/html");
            // TODO: do this at the end...
            //_response->addHeader("Cache-Control", "no-cache");
            //_request->send(response);
            _header_sent = true;
        }

        //_response->write(buffer, length);
        return _fs->write(buffer,length);
       /* size_t index = 0;
        while (index < length) {
            size_t copylen = std::min(length - index, BUFLEN - _buflen);
            memcpy(_buffer + _buflen, buffer + index, copylen);
            _buflen += copylen;
            index += copylen;
            if (_buflen >= BUFLEN) {  // The > case should not happen
                log_info("We shouldnt be calling flush! Buffer is too small!");
                _buflen=0;
                //flush();
            }
        }*/

        return length;
    }

    size_t WebClient::write(uint8_t data) {
        return write(&data, 1);
    }

    void WebClient::flush() {
        /*if (_request && _buflen) {
            _response->write((uint8_t *)_buffer, _buflen);
            //_request->sendContent(_buffer, _buflen);
            _buflen = 0;
        }*/
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
        //flush();
       // _request->send(_response); //200,"text/plain","");//close connection (?)
    }
}
