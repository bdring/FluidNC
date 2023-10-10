// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.
#include <ESPmDNS.h>
#include "../Machine/MachineConfig.h"
#include "TelnetClient.h"
#include "TelnetServer.h"
#include "WebSettings.h"

#ifdef ENABLE_WIFI

namespace WebUI {
    TelnetServer telnetServer __attribute__((init_priority(107)));
}

#    include "WifiServices.h"

#    include "WifiConfig.h"
#    include "../Report.h"  // report_init_message()
#    include "Commands.h"   // COMMANDS

#    include <WiFi.h>

namespace WebUI {

    EnumSetting* telnet_enable;
    IntSetting*  telnet_port;

    TelnetServer::TelnetServer() {
        telnet_port = new IntSetting(
            "Telnet Port", WEBSET, WA, "ESP131", "Telnet/Port", DEFAULT_TELNETSERVER_PORT, MIN_TELNET_PORT, MAX_TELNET_PORT, NULL);

        telnet_enable = new EnumSetting("Telnet Enable", WEBSET, WA, "ESP130", "Telnet/Enable", DEFAULT_TELNET_STATE, &onoffOptions, NULL);
    }

    bool TelnetServer::begin() {
        bool no_error = true;
        end();

        if (!WebUI::telnet_enable->get()) {
            return false;
        }
        _port = WebUI::telnet_port->get();

        //create instance
        _wifiServer = new WiFiServer(_port, MAX_TLNT_CLIENTS);
        _wifiServer->setNoDelay(true);
        log_info("Telnet started on port " << _port);
        //start telnet server
        _wifiServer->begin();
        _setupdone = true;

        //add mDNS
        if (WebUI::wifi_sta_ssdp->get()) {
            MDNS.addService("telnet", "tcp", _port);
        }

        return no_error;
    }

    void TelnetServer::end() {
        _setupdone = false;
        if (_wifiServer) {
            // delete _wifiServer;
            _wifiServer = NULL;
        }

        //remove mDNS
        mdns_service_remove("_telnet", "_tcp");
    }

    void TelnetServer::handle() {
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
            WiFiClient* tcpClient = new WiFiClient(_wifiServer->available());
            if (!tcpClient) {
                log_error("Creating telnet client failed");
            }
            log_debug("Telnet from " << tcpClient->remoteIP());
            TelnetClient* tnc = new TelnetClient(tcpClient);
            allChannels.registration(tnc);
        }
    }
    TelnetServer::~TelnetServer() { end(); }
}

#endif
