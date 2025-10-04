// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
// #include <ESPmDNS.h>
#include "Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"

#include "Mdns.h"
#include "Report.h"  // report_init_message()

#include <WiFi.h>

namespace WebUI {

    EnumSetting* telnet_enable;
    IntSetting*  telnet_port;

    uint16_t TelnetServer::_port = 0;

    std::queue<TelnetClient*> TelnetServer::_disconnected;

    void TelnetServer::init() {
        if (WiFi.getMode() == WIFI_OFF) {
            return;
        }

        deinit();

        telnet_port =
            new IntSetting("Telnet Port", WEBSET, WA, "ESP131", "Telnet/Port", DEFAULT_TELNETSERVER_PORT, MIN_TELNET_PORT, MAX_TELNET_PORT);

        telnet_enable = new EnumSetting("Telnet Enable", WEBSET, WA, "ESP130", "Telnet/Enable", DEFAULT_TELNET_STATE, &onoffOptions);

        if (!WebUI::telnet_enable->get()) {
            return;
        }
        _port = WebUI::telnet_port->get();

        //create instance
        _wifiServer = new WiFiServer(_port, MAX_TLNT_CLIENTS);
        _wifiServer->setNoDelay(true);
        log_info("Telnet started on port " << _port);
        //start telnet server
        _wifiServer->begin();
        _setupdone = true;

        Mdns::add("_telnet", "_tcp", _port);
    }

    void TelnetServer::deinit() {
        _setupdone = false;
        if (_wifiServer) {
            // delete _wifiServer;
            _wifiServer = NULL;
        }

        //remove mDNS
        Mdns::remove("_telnet", "_tcp");
    }

    void TelnetServer::poll() {
        if (!_setupdone || _wifiServer == NULL) {
            return;
        }

        while (_disconnected.size()) {
            log_debug("Telnet client disconnected");
            TelnetClient* client = _disconnected.front();
            _disconnected.pop();
            allChannels.deregistration(client);
            delete client;
        }

        //check if there are any new clients
        if (_wifiServer->hasClient()) {
            WiFiClient* tcpClient = new WiFiClient(_wifiServer->accept());
            if (!tcpClient) {
                log_error("Creating telnet client failed");
            }
            log_debug("Telnet from " << tcpClient->remoteIP());
            TelnetClient* tnc = new TelnetClient(tcpClient);
            allChannels.registration(tnc);
        }
    }

    void TelnetServer::status_report(Channel& out) {
        log_stream(out, "Data port: " << port());
    }

    TelnetServer::~TelnetServer() {
        deinit();
    }

    ModuleFactory::InstanceBuilder<TelnetServer> __attribute__((init_priority(109))) telnet_module("telnet_server", true);
}
