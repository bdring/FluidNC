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

    WSChannel::WSChannel(AsyncWebSocket* server, uint32_t clientNum, std::string session) : Channel("websocket"), _server(server), _clientNum(clientNum), _session(session) {}

    void WSChannel::active(bool is_active){
        _active=is_active;
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
        // logging to uart0 causes the esp to crash and reboot (because of watchdog) when running a longer return command such as $$...
        // not too sure why, I guess this may be the watchdog short timeout and by deasign, but perhaps there is something wrong with the code integrity
        // after all the async migration, or it is just something that gets called recursivelly somehow in parent Channels... 
        // should be tried in original non async code to confirm

        // With the session cookie we no longer need to broadcast to all
        //_server->binaryAll(out, outlen);
        if (!_server->binary(_clientNum, out, outlen)) {
             _active =  false;
        }
        //if(_output_line == "$10=1\n")
        //    _server->binaryAll(std::string("ok\n").c_str(), 3);
        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    bool WSChannel::sendTXT(std::string& s) {
        if (!_active) {
            return false;
        }

        // With the session cookie we no longer need to broadcast to all
        //_server->textAll(s.c_str());
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
        static AsyncWebSocket *_server;
    WSChannel::~WSChannel() {
        log_info_to(Uart0,"WSChannel destructor");
    }

    std::map<uint32_t, WSChannel*> WSChannels::_wsChannels;
    std::map<std::string, WSChannel*> WSChannels::_wsChannelsBySession;
    std::list<WSChannel*>          WSChannels::_webWsChannels;
    AsyncWebSocket *  WSChannels::_server = nullptr;

    WSChannel* WSChannels::_lastWSChannel = nullptr;

    WSChannel* WSChannels::getWSChannel(std::string session) {
        try {
                return _wsChannelsBySession.at(session);
            } catch (std::out_of_range& oor) {}
        if(_webWsChannels.size()>0)
                return  _webWsChannels.front();
        return nullptr;
    }

    WSChannel* WSChannels::getWSChannel(int pageid) {
        pageid=-1;
        WSChannel* wsChannel = nullptr;
        if (pageid != -1) {
            try {
                wsChannel = _wsChannels.at(pageid);
            } catch (std::out_of_range& oor) {}
        } else {
            if(_webWsChannels.size()>0)
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
            WSChannel* wsChannel = _wsChannels.at(num);
            std::string session = wsChannel->session();
            wsChannel->active(false);
            allChannels.kill(wsChannel);
            _webWsChannels.remove(wsChannel);
            _wsChannels.erase(num);
            _wsChannelsBySession.erase(session);
        } catch (std::out_of_range& oor) {}
    }

    // void WSChannels::removeChannel(WSChannel* channel) {
    //     _lastWSChannel = nullptr;
    //     channel->active(false);
    //     allChannels.kill(channel);
    //     _webWsChannels.remove(channel);
    //     for (auto it = _wsChannels.cbegin(); it != _wsChannels.cend();) {
    //         if (it->second == channel) {
    //              it = _wsChannels.erase(it);
    //         } else {
    //             ++it;
    //         }
    //     }
    // }

    bool WSChannels::runGCode(int pageid, std::string_view cmd, std::string session) {
        //log_info_to(Uart0, "runGCode session: " + session);
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
                        _cmd+='\n';
                    //settings_execute_line(_cmd.c_str(), *wsChannel, AuthenticationLevel::LEVEL_ADMIN);
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

    void WSChannels::handleEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len, std::string session){
        uint32_t num = client->id();
        _server = server;
        switch (type) {
            case WS_EVT_DISCONNECT:
                log_info_to(Uart0, "WebSocket disconnect " << num);
                WSChannels::removeChannel(num);
                break;
            case WS_EVT_CONNECT: {
                WSChannel* wsChannel = new WSChannel(server, num, session);
                if (!wsChannel) {
                    log_error_to(Uart0, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());

                    IPAddress ip = client->remoteIP();
                    log_info_to(Uart0, "WebSocket " << num << " from " << ip << " uri " << uri << " session " << session);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);

                    _wsChannels[num] = wsChannel;
                    _wsChannelsBySession[session] = wsChannel;

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

    void WSChannels::handlev3Event(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len, std::string session){
        // TODO: I may have wrongly assumed that we were referencing client in the channels array, but those are pageIDs?
        uint32_t num = client->id();
        _server = server;
        switch (type) {
            case WS_EVT_ERROR:
                log_info_to(Uart0, "WebSocket error cid#" << num << " total " << _wsChannels.size());
                WSChannels::removeChannel(num);
                break;
            case WS_EVT_DISCONNECT:
                log_info_to(Uart0, "WebSocket disconnect cid#" << num << " total " << _wsChannels.size());
                WSChannels::removeChannel(num);
                break;
            case WS_EVT_CONNECT: {
                log_info_to(Uart0, "WStype_Connected cid#" << num);
                WSChannel* wsChannel = new WSChannel(server, num, session);
                if (!wsChannel) {
                    log_error_to(Uart0, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());

                    IPAddress ip = client->remoteIP();
                    log_info_to(Uart0, "WebSocket " << num << " from " << ip << " uri " << uri << " session " << session);

                    _lastWSChannel = wsChannel;
                    allChannels.registration(wsChannel);
                    _wsChannels[num] = wsChannel;
                    _wsChannelsBySession[session] = wsChannel;
                    if (uri == "/") {
                        std::string s("currentID:");
                        s += std::to_string(num);
                        // send message to client
                        _webWsChannels.push_front(wsChannel);
                        wsChannel->sendTXT(s);
                        s = "activeID:";
                        s += std::to_string(wsChannel->id());
                        wsChannel->sendTXT(s);
                    }
                }
                log_info_to(Uart0, "WebSocket channels count: " << _wsChannels.size());
            } break;
            case WS_EVT_DATA: {
                AwsFrameInfo * info = (AwsFrameInfo*)arg;
                if(info->opcode == WS_TEXT){
                    try {
                        //data[len]=0; // !!! ?
                        
                        std::string msg((const char*)data, len);
                        //log_info_to(Uart0, msg);
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
                log_info_to(Uart0, "WebSocket unexpected event! " << type);
                break;
        }
    }
}
