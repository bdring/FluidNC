// Copyright (c) 2024 Mitch Bradley All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "Settings.h"
#include "esp_wifi.h"
#include <mdns.h>

namespace WebUI {
    class Mdns : public Module {
        static EnumSetting* _enable;

    public:
        Mdns(const char* name) : Module(name) {}

        void        init() override;
        void        deinit() override;
        static void add(const char* service, const char* proto, uint16_t port);
        static void remove(const char* service, const char* proto);
        ~Mdns() {}
    };
}
