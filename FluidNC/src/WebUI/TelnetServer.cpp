// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Machine/MachineConfig.h"
#include "TelnetServer.h"
#include "WebSettings.h"

#ifdef ENABLE_WIFI

namespace WebUI {
    Telnet_Server telnet_server;
}

#    include "WifiServices.h"

#    include "WifiConfig.h"
#    include "../Report.h"  // report_init_message()
#    include "Commands.h"   // COMMANDS

#    include <WiFi.h>

namespace WebUI {
    bool        Telnet_Server::_setupdone    = false;
    uint16_t    Telnet_Server::_port         = 0;
    WiFiServer* Telnet_Server::_telnetserver = NULL;
    WiFiClient  Telnet_Server::_telnetClients[MAX_TLNT_CLIENTS];

    EnumSetting* telnet_enable;
    IntSetting*  telnet_port;

    IPAddress Telnet_Server::_telnetClientsIP[MAX_TLNT_CLIENTS];

    Telnet_Server::Telnet_Server() : Channel("telnet"), _RXbufferSize(0), _RXbufferpos(0) {
        telnet_port = new IntSetting(
            "Telnet Port", WEBSET, WA, "ESP131", "Telnet/Port", DEFAULT_TELNETSERVER_PORT, MIN_TELNET_PORT, MAX_TELNET_PORT, NULL);

        telnet_enable = new EnumSetting("Telnet Enable", WEBSET, WA, "ESP130", "Telnet/Enable", DEFAULT_TELNET_STATE, &onoffOptions, NULL);
    }

    bool Telnet_Server::begin() {
        bool no_error = true;
        end();
        _RXbufferSize = 0;
        _RXbufferpos  = 0;

        if (!WebUI::telnet_enable->get()) {
            return false;
        }
        _port = WebUI::telnet_port->get();

        //create instance
        _telnetserver = new WiFiServer(_port, MAX_TLNT_CLIENTS);
        _telnetserver->setNoDelay(true);
        log_info("Telnet started on port " << _port);
        //start telnet server
        _telnetserver->begin();
        _setupdone = true;
        allChannels.registration(&telnet_server);
        return no_error;
    }

    void Telnet_Server::end() {
        _setupdone    = false;
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
        if (_telnetserver) {
            allChannels.deregistration(&telnet_server);
            delete _telnetserver;
            _telnetserver = NULL;
        }
    }

    void Telnet_Server::clearClients() {
        //check if there are any new clients
        if (_telnetserver->hasClient()) {
            size_t i;
            for (i = 0; i < MAX_TLNT_CLIENTS; i++) {
                //find free/disconnected spot
                if (!_telnetClients[i] || !_telnetClients[i].connected()) {
                    _telnetClientsIP[i] = IPAddress(0, 0, 0, 0);
                    if (_telnetClients[i]) {
                        _telnetClients[i].stop();
                    }
                    _telnetClients[i] = _telnetserver->available();
                    break;
                }
            }
            if (i >= MAX_TLNT_CLIENTS) {
                //no free/disconnected spot so reject
                _telnetserver->available().stop();
            }
        }
    }

    size_t Telnet_Server::write(uint8_t data) { return write(&data, 1); }

    size_t Telnet_Server::write(const uint8_t* buffer, size_t length) {
        if (!_setupdone || _telnetserver == NULL) {
            return 0;
        }
        clearClients();

        // Replace \n with \r\n
        size_t  rem      = length;
        uint8_t lastchar = '\0';
        size_t  j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                uint8_t c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }

            //push data to all connected telnet clients
            for (size_t i = 0; i < MAX_TLNT_CLIENTS; i++) {
                if (_telnetClients[i] && _telnetClients[i].connected()) {
                    _telnetClients[i].write(modbuf, k);
                    COMMANDS::wait(0);
                }
            }
        }
        return length;
    }

    void Telnet_Server::handle() {
        COMMANDS::wait(0);
        //check if can read
        if (!_setupdone || _telnetserver == NULL) {
            return;
        }
        clearClients();
        //check clients for data
        for (size_t i = 0; i < MAX_TLNT_CLIENTS; i++) {
            if (_telnetClients[i] && _telnetClients[i].connected()) {
                if (_telnetClientsIP[i] != _telnetClients[i].remoteIP()) {
                    report_init_message(telnet_server);
                    _telnetClientsIP[i] = _telnetClients[i].remoteIP();
                }
                if (_telnetClients[i].available()) {
                    uint8_t buf[1024];
                    COMMANDS::wait(0);
                    int readlen  = _telnetClients[i].available();
                    int writelen = TELNETRXBUFFERSIZE - available();
                    if (readlen > 1024) {
                        readlen = 1024;
                    }
                    if (readlen > writelen) {
                        readlen = writelen;
                    }
                    if (readlen > 0) {
                        _telnetClients[i].read(buf, readlen);
                        push(buf, readlen);
                    }
                    return;
                }
            } else {
                if (_telnetClients[i]) {
                    _telnetClientsIP[i] = IPAddress(0, 0, 0, 0);
                    _telnetClients[i].stop();
                }
            }
            COMMANDS::wait(0);
        }
    }

    int Telnet_Server::peek(void) {
        if (_RXbufferSize > 0) {
            return _RXbuffer[_RXbufferpos];
        } else {
            return -1;
        }
    }

    int Telnet_Server::available() { return _RXbufferSize; }

    int Telnet_Server::rx_buffer_available() { return TELNETRXBUFFERSIZE - _RXbufferSize; }

    bool Telnet_Server::push(uint8_t data) {
        if ((1 + _RXbufferSize) <= TELNETRXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > TELNETRXBUFFERSIZE) {
                current = current - TELNETRXBUFFERSIZE;
            }
            if (current > (TELNETRXBUFFERSIZE - 1)) {
                current = 0;
            }
            _RXbuffer[current] = data;
            _RXbufferSize++;
            return true;
        }
        return false;
    }

    bool Telnet_Server::push(const uint8_t* data, int data_size) {
        if ((data_size + _RXbufferSize) <= TELNETRXBUFFERSIZE) {
            int data_processed = 0;
            int current        = _RXbufferpos + _RXbufferSize;
            if (current > TELNETRXBUFFERSIZE) {
                current = current - TELNETRXBUFFERSIZE;
            }
            for (int i = 0; i < data_size; i++) {
                if (current > (TELNETRXBUFFERSIZE - 1)) {
                    current = 0;
                }

                _RXbuffer[current] = data[i];
                current++;
                data_processed++;

                COMMANDS::wait(0);
                //vTaskDelay(1 / portTICK_RATE_MS);  // Yield to other tasks
            }
            _RXbufferSize += data_processed;
            return true;
        }
        return false;
    }

    int Telnet_Server::read(void) {
        if (_RXbufferSize > 0) {
            int v = _RXbuffer[_RXbufferpos];
            _RXbufferpos++;
            if (_RXbufferpos > (TELNETRXBUFFERSIZE - 1)) {
                _RXbufferpos = 0;
            }
            _RXbufferSize--;
            return v;
        } else {
            return -1;
        }
    }

    Telnet_Server::~Telnet_Server() { end(); }
}

#endif
