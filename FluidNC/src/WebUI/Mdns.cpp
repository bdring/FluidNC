// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"
#include "Mdns.h"
#include <WiFi.h>

namespace WebUI {
    EnumSetting* Mdns::_enable;
#ifdef PIO_UNIT_TESTING
    namespace {
        bool g_mdnsEnabled = true;
    }
#endif

    void Mdns::init() {
#ifdef PIO_UNIT_TESTING
        if (WiFi.getMode() == WIFI_STA && g_mdnsEnabled) {
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
#else
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
#endif
    }

    void Mdns::deinit() {
        mdns_free();
    }
    void Mdns::add(const char* service, const char* proto, uint16_t port) {
#ifdef PIO_UNIT_TESTING
        if (WiFi.getMode() == WIFI_STA && g_mdnsEnabled) {
#else
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
#endif
            mdns_service_add(NULL, service, proto, port, NULL, 0);
        }
    }
    void Mdns::remove(const char* service, const char* proto) {
#ifdef PIO_UNIT_TESTING
        if (WiFi.getMode() == WIFI_STA && g_mdnsEnabled) {
#else
        if (WiFi.getMode() == WIFI_STA && _enable->get()) {
#endif
            mdns_service_remove(service, proto);
        }
    }

#ifndef PIO_UNIT_TESTING
    ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
#endif
}
