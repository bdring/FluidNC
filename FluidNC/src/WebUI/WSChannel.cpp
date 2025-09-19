// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"

#include "src/UartChannel.h"
#include "WebUIServer.h"
//#include <WebSocketsServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "src/Serial.h"  // is_realtime_command

namespace WebUI {
    class WSChannels;

    WSChannel::WSChannel(AsyncWebSocket* server, uint32_t clientNum) : Channel("websocket"), _server(server), _clientNum(clientNum) {}

    int WSChannel::read() {
        if (!_active) {
            return -1;
        }
        if (_rtchar == -1) {
            return -1;
        } else {
            auto ret = _rtchar;
            _rtchar  = -1;
            return ret;
        }
    }

    WSChannel::operator bool() const {
        return true;
    }

    size_t WSChannel::write(uint8_t c) {
        return write(&c, 1);
    }

    size_t WSChannel::write(const uint8_t* buffer, size_t size) {
        if (buffer == NULL || !_active || !size) {
            return 0;
        }

        bool complete_line = buffer[size - 1] == '\n';

        const uint8_t* out;
        size_t         outlen;
        if (_output_line.length() == 0 && complete_line) {
            // Avoid the overhead of std::string if the
            // input is a complete line and nothing is pending.
            out    = buffer;
            outlen = size;
        } else {
            // Otherwise collect input until we have line.
            _output_line.append((char*)buffer, size);
            if (!complete_line) {
                return size;
            }

            out    = (uint8_t*)_output_line.c_str();
            outlen = _output_line.length();
        }
        if (!_server->binary(_clientNum, out, outlen)) {
            _active = false;
        }
        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    bool WSChannel::sendTXT(std::string& s) {
        if (!_active) {
            return false;
        }
        if (!_server->text(_clientNum, s.c_str())) {
            _active = false;
            log_debug_to(Uart0, "WebSocket is unresponsive; closing");
            return false;
        }
        return true;
    }

    void WSChannel::autoReport() {
        if (!_active) {
            return;
        }
        /*int stat = _server->canSend(_clientNum);
        if (stat < 0) {
            _active = false;
            log_debug_to(Uart0, "WebSocket is dead; closing");
            return;
        }
        if (stat == 0) {
            return;
        }*/

        Channel::autoReport();
    }

    WSChannel::~WSChannel() {}

    std::map<uint32_t, WSChannel*> WSChannels::_wsChannels;
    std::list<WSChannel*>          WSChannels::_webWsChannels;

    WSChannel* WSChannels::_lastWSChannel = nullptr;

    WSChannel* WSChannels::getWSChannel(int pageid) {
        WSChannel* wsChannel = nullptr;
        if (pageid != -1) {
            try {
                wsChannel = _wsChannels.at(pageid);
            } catch (std::out_of_range& oor) {}
        } else {
            // If there is no PAGEID URL argument, it is an old version of WebUI
            // that does not supply PAGEID in all cases.  In that case, we use
            // the most recently used websocket if it is still in the list.
            for (auto it = _wsChannels.begin(); it != _wsChannels.end(); ++it) {
                if (it->second == _lastWSChannel) {
                    wsChannel = _lastWSChannel;
                    break;
                }
            }
        }
        _lastWSChannel = wsChannel;
        return wsChannel;
    }

    void WSChannels::removeChannel(uint32_t num) {
        try {
            WSChannel* wsChannel = _wsChannels.at(num);
            _webWsChannels.remove(wsChannel);
            allChannels.kill(wsChannel);
            _wsChannels.erase(num);
        } catch (std::out_of_range& oor) {}
    }

    void WSChannels::removeChannel(WSChannel* channel) {
        _lastWSChannel = nullptr;
        _webWsChannels.remove(channel);
        allChannels.kill(channel);
        for (auto it = _wsChannels.cbegin(); it != _wsChannels.cend();) {
            if (it->second == channel) {
                it = _wsChannels.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool WSChannels::runGCode(int pageid, std::string_view cmd) {
        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            if (cmd.length()) {
                if (is_realtime_command(cmd[0])) {
                    for (auto const& c : cmd) {
                        wsChannel->handleRealtimeCharacter((uint8_t)c);
                    }
                } else {
                    wsChannel->push(cmd);
                    if (cmd.back() != '\n') {
                        wsChannel->push('\n');
                    }
                }
            }
            return false;
        }
        return true;  // Error - no websocket
    }

    bool WSChannels::sendError(int pageid, std::string err) {
        WSChannel* wsChannel = getWSChannel(pageid);
        if (wsChannel) {
            return !wsChannel->sendTXT(err);
        }
        return true;
    }
    void WSChannels::sendPing() {
        for (WSChannel* wsChannel : _webWsChannels) {
            std::string s("PING:");
            s += std::to_string(wsChannel->id());
            // sendBIN would be okay too because the string contains only
            // ASCII characters, no UTF-8 extended characters.
            wsChannel->sendTXT(s);
        }
    }

    void WSChannels::handleEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
        uint32_t num = client->id();
        switch (type) {
            case WS_EVT_DISCONNECT:
                log_debug_to(Uart0, "WebSocket disconnect " << num);
                WSChannels::removeChannel(num);
                break;
            case WS_EVT_CONNECT: {
                WSChannel* wsChannel = new WSChannel(server, num);
                if (!wsChannel) {
                    log_error_to(Uart0, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());

                    IPAddress ip = client->remoteIP();
                    log_debug_to(Uart0, "WebSocket " << num << " from " << ip << " uri " << uri);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;

                    if (uri == "/") {
                        std::string s("CURRENT_ID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "ACTIVE_ID:";
                        s += std::to_string(wsChannel->id());
                        wsChannel->sendTXT(s);
                    }
                }
            } break;
            case WS_EVT_DATA:
                try {
                    _wsChannels.at(num)->push(data, len);
                } catch (std::out_of_range& oor) {}
                break;
            default:
                break;
        }
    }

    //void WSChannels::handlev3Event(AsyncWebSocket* server, uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
    void WSChannels::handlev3Event(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len){
        uint32_t num = client->id();
        switch (type) {
            case WS_EVT_DISCONNECT:
                printf("WebSocket disconnect %d\n", num);
                WSChannels::removeChannel(num);
                break;
            case WS_EVT_CONNECT: {
                log_debug_to(Uart0, "WStype_Connected");
                WSChannel* wsChannel = new WSChannel(server, num);
                if (!wsChannel) {
                    log_error_to(Uart0, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());

                    IPAddress ip = client->remoteIP();
                    log_debug_to(Uart0, "WebSocket " << num << " from " << ip << " uri " << uri);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;

                    if (uri == "/") {
                        std::string s("currentID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "activeID:";
                        s += std::to_string(wsChannel->id());
                        server->textAll(s.c_str());
                    }
                }
            } break;
            case WS_EVT_DATA: {
                AwsFrameInfo * info = (AwsFrameInfo*)arg;
                if(info->opcode == WS_TEXT){
                    try {
                        data[len]=0;
                        std::string msg = (const char*)data;
                        if (msg.rfind("PING:", 0) == 0) {
                            std::string response("PING:60000:60000");
                            _wsChannels.at(num)->sendTXT(response);
                        } else
                            _wsChannels.at(num)->push(data, len);
                    } catch (std::out_of_range& oor) {}
                }
                else
                {
                    try {
                        _wsChannels.at(num)->push(data, len);
                    } catch (std::out_of_range& oor) {}
                }
            } break;
            default:
                break;
        }
    }
}
