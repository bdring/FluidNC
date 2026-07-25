// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"
#include "Driver/fluidnc_mdns.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <esp_err.h>
#include <mdns.h>

namespace WebUI {
    EnumSetting* Mdns::_enable;

    void Mdns::init() {
        _enable = new EnumSetting("mDNS enable", WEBSET, WA, NULL, "MDNS/Enable", true, &onoffOptions);

        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            if (mdns_init()) {
                log_error("Cannot start mDNS");
                return;
            }
            const char* h = WiFi.getHostname();
            if (mdns_hostname_set(h)) {
                log_error("Cannot set mDNS hostname to " << h);
                return;
            }
            log_info("Start mDNS with hostname:http://" << h << ".local/");
        }
    }

    void Mdns::deinit() {
        mdns_free();
    }
    void Mdns::add(const char* service, const char* proto, uint16_t port) {
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            mdns_service_add(NULL, service, proto, port, NULL, 0);
        }
    }
    void Mdns::remove(const char* service, const char* proto) {
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
            mdns_service_remove(service, proto);
        }
    }
    void Mdns::poll() {}

    ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
}
