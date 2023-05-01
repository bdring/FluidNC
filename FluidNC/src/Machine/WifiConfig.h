// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include <IPAddress.h>

namespace Machine {
    class WifiConfig : public Configuration::Configurable {
    public:
        IPAddress _ipAddress;
        IPAddress _gateway;
        IPAddress _netmask;

        WifiConfig() : _ipAddress(10, 0, 0, 1), _gateway(10, 0, 0, 1), _netmask(255, 255, 0, 0) {}

        std::string _ssid = "FluidNC";

        bool _dhcp = true;

        void group(Configuration::HandlerBase& handler) override {
            handler.item("ssid", _ssid);
            // No passwords in the config file!
            handler.item("ip_address", _ipAddress);
            handler.item("gateway", _gateway);
            handler.item("netmask", _netmask);

            handler.item("dhcp", _dhcp);
        }
    };
}
