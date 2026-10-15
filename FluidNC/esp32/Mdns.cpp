// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Module.h"
#include "Driver/fluidnc_mdns.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <esp_err.h>
#include <mdns.h>

// Defined in WebUI/EthConfig.cpp when Ethernet support is compiled in;
// null in builds without it.
extern bool fluidnc_ethernet_active() __attribute__((weak));

namespace WebUI {
    EnumSetting* Mdns::_enable;

    // mDNS needs a client-side interface: WiFi STA or Ethernet.  The IDF
    // mdns component already binds to the netif that Arduino's ETH creates,
    // so only this check kept it off for Ethernet.
    static bool mdnsInterfaceUp() {
        return WiFi.getMode() == WIFI_STA || (fluidnc_ethernet_active && fluidnc_ethernet_active());
    }

    void Mdns::init() {
        _enable = new EnumSetting("mDNS enable", WEBSET, WA, NULL, "MDNS/Enable", true, &onoffOptions);

        if (mdnsInterfaceUp() && _enable->get()) {
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
        if (mdnsInterfaceUp() && _enable->get()) {
            mdns_service_add(NULL, service, proto, port, NULL, 0);
        }
    }
    void Mdns::remove(const char* service, const char* proto) {
        if (mdnsInterfaceUp() && _enable->get()) {
            mdns_service_remove(service, proto);
        }
    }
    void Mdns::poll() {}

    ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
}
