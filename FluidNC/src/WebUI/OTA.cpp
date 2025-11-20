// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"

#include "Logging.h"
#include <WiFi.h>
#include "Driver/localfs.h"
#include <ArduinoOTA.h>

class OTA : public Module {
public:
    OTA(const char* name) : Module(name) {}

    void init() override {
        if (WiFi.getMode() == WIFI_OFF) {
            return;
        }

        ArduinoOTA
            // By default, ArduinoOTA starts MDNS and advertises itself to the ArduinoIDE
            // We don't care about the Arduino IDE, and we want to start MDNS explicitly
            // in Mdns.cpp
            .setMdnsEnabled(false)
            .setHostname(WiFi.getHostname())
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
            })
            .begin();
    }

    void deinit() override { ArduinoOTA.end(); }

    void poll() override { ArduinoOTA.handle(); }

    ~OTA() {}
};

ModuleFactory::InstanceBuilder<OTA> __attribute__((init_priority(106))) ota_module("ota", true);
