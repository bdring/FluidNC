#if 0
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#    include "WifiServices.h"

#    include "src/Machine/MachineConfig.h"

#    include "WifiConfig.h"
#    include "WebServer.h"
#    include "TelnetServer.h"
#    include "NotificationsService.h"

#    include <WiFi.h>
#    include <ESPmDNS.h>
#    include <ArduinoOTA.h>

namespace WebUI {
    WiFiServices wifi_services;

    WiFiServices::WiFiServices() : Module("wifi_services") {}
    WiFiServices::~WiFiServices() {
        end();
    }

    void WiFiServices::init() {
        //no need in AP mode
        // webServer.begin();
        // telnetServer.begin();
        // notificationsService.begin();
    }
    void WiFiServices::end() {
        // notificationsService.end();
        // telnetServer.end();
        // webServer.end();

        //stop OTA

        //Stop mDNS
    }

    void WiFiServices::poll() {}

    // Configuration registration
    namespace {
        ModuleFactory::InstanceBuilder<WiFiServices> registration("wifi_services", true);
    }
}
#endif
