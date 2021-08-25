// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

namespace WebUI {
    class WiFiServices {
    public:
        WiFiServices();

        static bool begin();
        static void end();
        static void handle();

        ~WiFiServices();
    };

    extern WiFiServices wifi_services;
}
