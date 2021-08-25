// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"

#include "../WebUI/BTConfig.h"
#include "../Config.h"  // ENABLE_*
#include "WifiAPConfig.h"
#include "WifiSTAConfig.h"

#include <WString.h>
#include <IPAddress.h>

namespace WebUI {
    class BTConfig;
}

namespace Machine {
    class Communications : public Configuration::Configurable {
    public:
        Communications() = default;

        bool _telnetEnable = true;
        int  _telnetPort   = 23;

        bool _httpEnable = true;
        int  _httpPort   = 80;

        String _hostname = "fluidnc";

        WebUI::BTConfig* _bluetoothConfig = nullptr;
        WifiAPConfig*    _apConfig        = nullptr;
        WifiSTAConfig*   _staConfig       = nullptr;

        void group(Configuration::HandlerBase& handler) override {
#ifdef ENABLE_BLUETOOTH
            // If BT is not compiled in, attempts to configure it will be ignored,
            // but the BTConfig class methods that might be called from elsewhere
            // exist in an stub class implementation, so the compiler will not
            // complain.  This lets us minimize the number of ifdefs to this one
            // plus a couple in BTConfig.h and BTConfig.cpp which select either
            // the real BTConfig class or the stub version.
            handler.section("bluetooth", _bluetoothConfig);
#endif
#ifdef ENABLE_WIFI
            handler.item("telnet_enable", _telnetEnable);
            handler.item("telnet_port", _telnetPort);

            handler.item("http_enable", _httpEnable);
            handler.item("http_port", _httpPort);

            handler.item("hostname", _hostname);

            handler.section("wifi_ap", _apConfig);
            handler.section("wifi_sta", _staConfig);
#endif
        }

        ~Communications() {
            delete _bluetoothConfig;
            delete _apConfig;
            delete _staConfig;
        }
    };
}
