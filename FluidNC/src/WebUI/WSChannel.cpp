// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"

#include "src/UartChannel.h"
#include "WebUIServer.h"
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "src/System.h"

#include "src/Serial.h"  // is_realtime_command

namespace WebUI {
    class WSChannels;

    WSChannel::WSChannel(AsyncWebSocket* server, uint32_t clientNum, std::string session) :
        Channel("websocket"), _server(server), _clientNum(clientNum), _session(session) {
        setReportInterval(200);  // we will set automatic reporting on by default for now
        _server->client(_clientNum)->setCloseClientOnQueueFull(false);
    }

    void WSChannel::active(bool is_active) {
        _active = is_active;
    }
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
        // With the session cookie we no longer need to broadcast to all
        //_server->binaryAll(out, outlen);

        // For commands like $esp400, buffering multiple lines into one websocket message would be faster,
        // however we don't get any event when the command response is completed,
        // some commands respond with "ok" at the end, but not all of them.
        // Also, for larges response commands (again like $esp400), there is just too many lines
        // in the response (>32KB of json), so we need to check if the websocket buffer is full before continuing
        // The delay seems to do the trick.
        // It would be a lot better to always force these commands to return as a http response instead of websocket,
        // however, Webui(3) expects the command $$ to come back from a websocket, which is at least one reason why we can't send all back as a http response
        if (!inMotionState()) {
            while (_server->client(_clientNum) && _server->client(_clientNum)->queueLen() >= max(WS_MAX_QUEUED_MESSAGES - 2, 1)) {
                delay(1);
            }
        } else {
            // To test this mechanism, try setting WS_MAX_QUEUED_MESSAGES to 2 and have 2 browsers on different PCs or your smartphone
            if (_server->client(_clientNum) && _server->client(_clientNum)->queueIsFull() && (millis() - _last_queue_full) > 1000) {
                _last_queue_full = millis();
                log_debug_to(Uart0, "Websocket queue full while sending to cid#" << _clientNum << ", dropping");
            }
        }
        // No need to set active false, we continue to send and let the websocket drop if buffer is too high
        // and disconnect if client timeout
        if (!_server->binary(_clientNum, out, outlen)) {
            // _active =  false;
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
            return false;
        }
        return true;
    }

    void WSChannel::autoReport() {
        if (!_active) {
            return;
        }
        Channel::autoReport();
    }

    static AsyncWebSocket* _server;
    WSChannel::~WSChannel() {}

    std::map<uint32_t, WSChannel*>    WSChannels::_wsChannels;
    std::map<std::string, WSChannel*> WSChannels::_wsChannelsBySession;
    std::list<WSChannel*>             WSChannels::_webWsChannels;
    AsyncWebSocket*                   WSChannels::_server = nullptr;

    WSChannel* WSChannels::_lastWSChannel = nullptr;

    WSChannel* WSChannels::getWSChannel(std::string session) {
        try {
            return _wsChannelsBySession.at(session);
        } catch (std::out_of_range& oor) {}
        if (_webWsChannels.size() > 0)
            return _webWsChannels.front();
        return nullptr;
    }

    WSChannel* WSChannels::getWSChannel(int pageid) {
        pageid               = -1;
        WSChannel* wsChannel = nullptr;
        if (pageid != -1) {
            try {
                wsChannel = _wsChannels.at(pageid);
            } catch (std::out_of_range& oor) {}
        } else {
            if (_webWsChannels.size() > 0)
                wsChannel = _webWsChannels.front();

            // // If there is no PAGEID URL argument, it is an old version of WebUI
            // // that does not supply PAGEID in all cases.  In that case, we use
            // // the most recently used websocket if it is still in the list.
            // for (auto it = _wsChannels.begin(); it != _wsChannels.end(); ++it) {
            //     if (it->second == _lastWSChannel) {
            //         wsChannel = _lastWSChannel;
            //         break;
            //     }
            // }
        }
        _lastWSChannel = wsChannel;
        return wsChannel;
    }

    void WSChannels::removeChannel(uint32_t num) {
        try {
            WSChannel*  wsChannel = _wsChannels.at(num);
            std::string session   = wsChannel->session();
            wsChannel->active(false);
            allChannels.kill(wsChannel);
            _webWsChannels.remove(wsChannel);
            _wsChannels.erase(num);
            // Only remove if this is the same object
            if (_wsChannelsBySession[session] == wsChannel)
                _wsChannelsBySession.erase(session);
        } catch (std::out_of_range& oor) {}
    }

    bool WSChannels::runGCode(int pageid, std::string_view cmd, std::string session) {
        WSChannel* wsChannel = getWSChannel(session);
        if (wsChannel) {
            if (cmd.length()) {
                if (is_realtime_command(cmd[0])) {
                    for (auto const& c : cmd) {
                        wsChannel->handleRealtimeCharacter((uint8_t)c);
                    }
                } else {
                    std::string _cmd = std::string(cmd);
                    if (_cmd.back() != '\n')
                        _cmd += '\n';
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

    bool WSChannels::sendError(int pageid, std::string err, std::string session) {
        WSChannel* wsChannel = getWSChannel(session);
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

    void WSChannels::handleEvent(
        AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len, std::string session) {
        uint32_t num = client->id();
        _server      = server;
        switch (type) {
            case WS_EVT_ERROR:
                WSChannels::removeChannel(num);
                log_debug_to(Uart0, "WebSocket error cid#" << num << " total " << _wsChannels.size());
                break;
            case WS_EVT_DISCONNECT:
                WSChannels::removeChannel(num);
                log_debug_to(Uart0, "WebSocket disconnect cid#" << num << " total " << _wsChannels.size());
                break;
            case WS_EVT_CONNECT: {
                WSChannel* wsChannel = new WSChannel(server, num, session);
                if (!wsChannel) {
                    log_error_to(Uart0, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());
                    IPAddress   ip = client->remoteIP();

                    // Ask any client with same session ID to disconnect
                    // This is to deal with miltiple tabs within the same browser having the same session,
                    // only deal with the last one connected, and disconnect the previous one.
                    if (_wsChannelsBySession[session]) {
                        WSChannel* oldClient = _wsChannelsBySession[session];
                        _wsChannelsBySession.erase(session);
                        std::string s("currentID:");  // webui3
                        s += std::to_string(0);       // valid client IDs start at 1
                        oldClient->sendTXT(s);
                        s = "CURRENT_ID:";  // webui2
                        s += std::to_string(0);
                        oldClient->sendTXT(s);
                        s = "activeID:";  // webui3
                        s += std::to_string(num);
                        oldClient->sendTXT(s);
                        s = "ACTIVE_ID:";  // webui2
                        s += std::to_string(num);
                        oldClient->sendTXT(s);
                    }
                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num]              = wsChannel;
                    _wsChannelsBySession[session] = wsChannel;
                    log_debug_to(Uart0,
                                 "WebSocket connect cid#" << num << " from " << ip << " uri " << uri << " session " << session << " total "
                                                          << _wsChannels.size());
                }
            } break;
            case WS_EVT_DATA: {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->opcode == WS_TEXT) {
                    try {
                        //data[len]=0; // !!! this should not be safe? but was there before,
                        // will copy to a std::string of specified length to be on the safe side
                        std::string msg((const char*)data, len);
                        if (msg.rfind("PING:", 0) == 0) {
                            std::string response("PING:60000:60000");
                            _wsChannels.at(num)->sendTXT(response);
                        } else
                            _wsChannels.at(num)->push(data, len);
                    } catch (std::out_of_range& oor) {}
                } else {
                    try {
                        _wsChannels.at(num)->push(data, len);
                    } catch (std::out_of_range& oor) {}
                }
            } break;
            default:
                log_debug_to(Uart0, "WebSocket unexpected event! " << type);
                break;
        }
    }
}
