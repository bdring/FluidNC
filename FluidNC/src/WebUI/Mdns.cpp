// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Module.h"
#include "Mdns.h"
#include <WiFi.h>
#include <ESPmDNS.h>

namespace WebUI {
    EnumSetting* mdns_enable;

    class Mdns : public Module {
    public:
        Mdns() : Module("mdns") {}

        void init() override {
            mdns_enable = new EnumSetting("mDNS enable", WEBSET, WA, NULL, "MDNS/Enable", true, &onoffOptions);

            printf("*** Mdns init\n");
            if (WiFi.getMode() == WIFI_STA && WebUI::mdns_enable->get()) {
                const char* h = WiFi.getHostname();
                if (!MDNS.begin(h)) {
                    log_info("Cannot start mDNS");
                } else {
                    log_info("Start mDNS with hostname:http://" << h << ".local/");
                }
            }
        }

        void deinit() override { MDNS.end(); }
        ~Mdns() {}
    };

    ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(107))) mdns_module("mdns", true);
}
