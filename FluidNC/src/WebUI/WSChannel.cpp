// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WSChannel.h"

#include "Driver/Console.h"
#include "WebUIServer.h"
#include <cstdio>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <freertos/semphr.h>
#include "System.h"

#include "Serial.h"  // is_realtime_command

namespace WebUI {
    class WSChannels;

    namespace {
        SemaphoreHandle_t ws_channels_mutex = xSemaphoreCreateMutex();

        struct WSChannelInfo {
            objnum_t    id;
            std::string session;
        };

        AsyncWebSocketClient* get_client(AsyncWebSocket* server, objnum_t client_num) {
            auto client = server ? server->client(client_num) : nullptr;
            return (client && client->status() == WS_CONNECTED) ? client : nullptr;
        }

        bool send_control_message(AsyncWebSocketClient* client, std::string_view message) {
            return client && client->status() == WS_CONNECTED && client->text(message.data(), message.length());
        }
    }

    WSChannel::WSChannel(AsyncWebSocket* server, objnum_t clientNum, std::string session) :
        Channel("websocket"), _server(server), _clientNum(clientNum), _session(session) {
        setReportInterval(200);  // we will set automatic reporting on by default for now
        if (auto client = get_client(_server, _clientNum)) {
            client->setCloseClientOnQueueFull(false);
        } else {
            _active = false;
        }
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

        auto client = get_client(_server, _clientNum);
        if (!client) {
            _active = false;
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
            const auto queue_limit = max(WS_MAX_QUEUED_MESSAGES - 2, 1);
            auto       wait_start  = millis();
            while ((client = get_client(_server, _clientNum)) && client->queueLen() >= queue_limit) {
                if ((millis() - wait_start) > 250) {
                    log_debug_to(Console, "Websocket queue stalled for cid#" << _clientNum << ", closing");
                    client->close();
                    _active = false;
                    return 0;
                }
                delay(1);
            }
        } else {
            // To test this mechanism, try setting WS_MAX_QUEUED_MESSAGES to 2 and have 2 browsers on different PCs or your smartphone
            if (client->queueIsFull() && (millis() - _last_queue_full) > 1000) {
                _last_queue_full = millis();
                log_debug_to(Console, "Websocket queue full while sending to cid#" << _clientNum << ", dropping");
            }
        }
        client = get_client(_server, _clientNum);
        if (!client) {
            _active = false;
            return 0;
        }
        // No need to set active false, we continue to send and let the websocket drop if buffer is too high
        // and disconnect if client timeout
        bool sent = false;
        try {
            sent = _server->binary(_clientNum, out, outlen);
        } catch (const std::exception&) {
            if (client) {
                client->close();
            }
            _active = false;
            return 0;
        } catch (...) {
            if (client) {
                client->close();
            }
            _active = false;
            return 0;
        }
        if (!sent) {
            if (client->queueIsFull()) {
                client->close();
            }
            _active = false;
        }

        if (_output_line.length()) {
            _output_line = "";
        }

        return size;
    }

    bool WSChannel::sendTXT(std::string_view s) {
        if (!_active) {
            return false;
        }

        auto client = get_client(_server, _clientNum);
        if (!client) {
            _active = false;
            return false;
        }

        bool sent = false;
        try {
            sent = _server->text(_clientNum, s.data(), s.length());
        } catch (const std::exception&) {
            client->close();
            _active = false;
            return false;
        } catch (...) {
            client->close();
            _active = false;
            return false;
        }
        if (!sent) {
            if (client->queueIsFull()) {
                client->close();
            }
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

    WSChannel::~WSChannel() {}

    std::vector<WSChannel*> WSChannels::_wsChannels;
    AsyncWebSocket*         WSChannels::_server = nullptr;

    std::vector<std::pair<pinnum_t, InputPin*>> WSChannels::_pins;

    WSChannel* WSChannels::_lastWSChannel = nullptr;
    WSChannel* WSChannels::getWSChannel(objnum_t pageid, std::string session) {
        xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
        for (auto it = _wsChannels.begin(); it < _wsChannels.end(); ++it) {
            if (pageid) {
                // Do not combine these predicates into a single to avoid
                // a match on session if pageid is 0.
                if ((*it)->id() == pageid) {
                    xSemaphoreGive(ws_channels_mutex);
                    return *it;
                }
            } else if ((*it)->session() == session) {
                xSemaphoreGive(ws_channels_mutex);
                return *it;
            }
        }
        xSemaphoreGive(ws_channels_mutex);
        return nullptr;
    }

    void WSChannels::removeChannel(WSChannel* channel) {
        if (channel) {
            removeChannel(channel->id());
        }
    }

    void WSChannels::removeChannel(objnum_t num) {
        xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
        for (auto it = _wsChannels.begin(); it < _wsChannels.end(); ++it) {
            if ((*it)->id() == num) {
                auto wsChannel = *it;
                // Stop accepting or exposing any queued websocket input immediately.
                // Otherwise the polling task can still promote stale commands that were
                // buffered just before the disconnect event was delivered.
                wsChannel->begin_closing();
                wsChannel->active(false);
                wsChannel->flushRx();

                _wsChannels.erase(it);
                allChannels.kill(wsChannel);
                break;
            }
        }
        xSemaphoreGive(ws_channels_mutex);
    }

    void WSChannels::showChannels() {
        std::vector<WSChannelInfo> wsChannels;
        {
            xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
            wsChannels.reserve(_wsChannels.size());
            for (auto const wsChannel : _wsChannels) {
                wsChannels.push_back({ wsChannel->id(), wsChannel->session() });
            }
            xSemaphoreGive(ws_channels_mutex);
        }

        log_debug("wsChannels: " << wsChannels.size());
        for (auto wsChannel : wsChannels) {
            log_debug("id " << wsChannel.id << " session " << wsChannel.session);
        }
    }

    bool WSChannels::runGCode(uint32_t pageid, std::string_view cmd, std::string session) {
        WSChannel* wsChannel = getWSChannel(pageid, session);
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

    bool WSChannels::sendError(uint32_t pageid, std::string err, std::string session) {
        WSChannel* wsChannel = getWSChannel(pageid, session);
        if (wsChannel) {
            return !wsChannel->sendTXT(err);
        }
        return true;
    }

    void WSChannels::closeSessionChannels(const std::string& session, objnum_t exceptId) {
        std::vector<objnum_t> channelIds;
        {
            xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
            for (auto const wsChannel : _wsChannels) {
                if (wsChannel->session() == session && wsChannel->id() != exceptId) {
                    channelIds.push_back(wsChannel->id());
                }
            }
            xSemaphoreGive(ws_channels_mutex);
        }

        for (auto const channelId : channelIds) {
            if (auto oldClient = get_client(_server, channelId)) {
                oldClient->close();
            }
        }
    }

    void WSChannels::sendPing() {
        std::vector<objnum_t> wsChannelIds;
        {
            xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
            wsChannelIds.reserve(_wsChannels.size());
            for (auto const wsChannel : _wsChannels) {
                wsChannelIds.push_back(wsChannel->id());
            }
            xSemaphoreGive(ws_channels_mutex);
        }

        for (auto const channelId : wsChannelIds) {
            if (_server) {
                _server->text(channelId, "PING\n", 5);
            }
        }
    }

    void WSChannels::handleEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        uint32_t num = client->id();
        _server      = server;
        switch (type) {
            case WS_EVT_ERROR:
                WSChannels::removeChannel(num);
                log_debug_to(Console, "WebSocket error cid#" << num << " " << std::string_view((char*)data, len));
                break;
            case WS_EVT_DISCONNECT:
                WSChannels::removeChannel(num);
                log_debug_to(Console, "WebSocket disconnect cid#" << num);
                break;
            case WS_EVT_CONNECT: {
                auto*      request    = static_cast<AsyncWebServerRequest*>(arg);
                auto       session    = request ? WebUI_Server::getWebSocketSession(request, client) : std::string {};
                WSChannel* newChannel = new WSChannel(server, num, session);
                if (!newChannel) {
                    log_error_to(Console, "Creating WebSocket channel failed");
                } else {
                    std::string uri((char*)server->url());
                    IPAddress   ip = client->remoteIP();

                    std::string s;
                    {
                        xSemaphoreTake(ws_channels_mutex, portMAX_DELAY);
                        _lastWSChannel = newChannel;
                        _wsChannels.push_back(newChannel);
                        xSemaphoreGive(ws_channels_mutex);
                    }

                    // The newest websocket for a session wins. Actively close any older
                    // sockets instead of waiting for the old page to cooperate.
                    closeSessionChannels(session, num);

                    allChannels.registration(newChannel);

                    // This tells WebUI the ID of the newly-created websocket
                    // so it can include that ID in a PAGEID= argument to
                    // direct output to that websocket

                    s = "currentID:";  // webui3
                    s += std::to_string(num);
                    send_control_message(client, s);

                    s = "CURRENT_ID:";  // webui2
                    s += std::to_string(num);
                    send_control_message(client, s);

                    log_debug_to(Console, "WebSocket connect cid#" << num << " from " << ip << " uri " << uri << " session " << session);
                    for (auto const pin : _pins) {
                        newChannel->registerEvent(pin.first, pin.second);
                    }
                }
            } break;
            case WS_EVT_DATA: {
                AwsFrameInfo* info      = (AwsFrameInfo*)arg;
                auto          wsChannel = getWSChannel(num, {});
                if (wsChannel) {
                    if (info->opcode == WS_TEXT) {
                        //data[len]=0; // !!! this should not be safe? but was there before,
                        // will copy to a std::string of specified length to be on the safe side
                        std::string msg((const char*)data, len);
                        if (msg.rfind("PING:", 0) == 0) {
                            wsChannel->sendTXT("PING:60000:60000");
                        } else {
                            wsChannel->push(data, len);
                        }
                    } else {
                        wsChannel->push(data, len);
                    }
                }
            } break;
            default:
                log_debug_to(Console, "WebSocket unexpected event! " << type);
                break;
        }
    }
    void WSChannels::registerEvent(pinnum_t index, InputPin* obj) {
        auto pinspec = std::pair<pinnum_t, InputPin*> { index, obj };
        _pins.push_back(pinspec);
    }
}
