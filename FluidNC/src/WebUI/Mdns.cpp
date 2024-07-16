// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Module.h"
#include "WifiConfig.h"
#include <ESPmDNS.h>

namespace WebUI {
    class Mdns : public Module {
    public:
        Mdns() : Module("mdns") {}

        void init() override {
            if (WiFi.getMode() == WIFI_STA && WebUI::wifi_sta_ssdp->get()) {
                const char* h = WebUI::wifi_hostname->get();
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

    namespace {
        ModuleFactory::InstanceBuilder<Mdns> __attribute__((init_priority(106))) registration("mdns", true);
    }
}
