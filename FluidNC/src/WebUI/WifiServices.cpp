// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WifiServices.h"

#include "../Machine/MachineConfig.h"

#ifndef ENABLE_WIFI
namespace WebUI {
    WiFiServices wifi_services;

    WiFiServices::WiFiServices() {}
    WiFiServices::~WiFiServices() { end(); }

    bool WiFiServices::begin() { return false; }
    void WiFiServices::end() {}
    void WiFiServices::handle() {}
}
#else
#    include "WifiConfig.h"
#    include "WebServer.h"
#    include "TelnetServer.h"
#    include "NotificationsService.h"
#    include "Commands.h"

#    include <WiFi.h>
#    include "Driver/localfs.h"
#    include <ESPmDNS.h>
#    include <ArduinoOTA.h>
#    include "WebSettings.h"

namespace WebUI {
    WiFiServices wifi_services;

    WiFiServices::WiFiServices() {}
    WiFiServices::~WiFiServices() { end(); }

    bool WiFiServices::begin() {
        bool no_error = true;

        if (WiFi.getMode() == WIFI_OFF) {
            return false;
        }

        ArduinoOTA
            .onStart([]() {
                const char* type;
                if (ArduinoOTA.getCommand() == U_FLASH) {
                    type = "sketch";
                } else {
                    type = "filesystem";
                    localfs_unmount();
                }
                log_info("Start OTA updating " << type);
            })
            .onEnd([]() { log_info("End OTA"); })
            .onProgress([](unsigned int progress, unsigned int total) { log_info("OTA Progress: " << (progress / (total / 100)) << "%"); })
            .onError([](ota_error_t error) {
                const char* errorName;
                switch (error) {
                    case OTA_AUTH_ERROR:
                        errorName = "Auth Failed";
                        break;
                    case OTA_BEGIN_ERROR:
                        errorName = "Begin Failed";
                        break;
                    case OTA_CONNECT_ERROR:
                        errorName = "Connect Failed";
                        break;
                    case OTA_RECEIVE_ERROR:
                        errorName = "Receive Failed";
                        break;
                    case OTA_END_ERROR:
                        errorName = "End Failed";
                        break;
                    default:
                        errorName = "Unknown";
                        break;
                }

                log_info("OTA Error(" << error << "):" << errorName);
            });
        ArduinoOTA.begin();
        //no need in AP mode
        if (WiFi.getMode() == WIFI_STA && WebUI::wifi_sta_ssdp->get()) {
            //start mDns
            const char* h = wifi_hostname->get();
            if (!MDNS.begin(h)) {
                log_info("Cannot start mDNS");
                no_error = false;
            } else {
                log_info("Start mDNS with hostname:http://" << h << ".local/");
            }
        }
        webServer.begin();
        telnetServer.begin();
        notificationsService.begin();

        //be sure we are not is mixed mode in setup
        WiFi.scanNetworks(true);
        return no_error;
    }
    void WiFiServices::end() {
        notificationsService.end();
        telnetServer.end();
        webServer.end();

        //stop OTA
        ArduinoOTA.end();

        //Stop mDNS
        MDNS.end();
    }

    void WiFiServices::handle() {
        //to avoid mixed mode due to scan network
        if (WiFi.getMode() == WIFI_AP_STA) {
            // In principle it should be sufficient to check for != WIFI_SCAN_RUNNING,
            // but that does not work well.  Doing so makes scans in AP mode unreliable.
            // Sometimes the first try works, but subsequent scans fail.
            if (WiFi.scanComplete() >= 0) {
                WiFi.enableSTA(false);
            }
        }
        ArduinoOTA.handle();
        webServer.handle();
        telnetServer.handle();
    }
}
#endif
