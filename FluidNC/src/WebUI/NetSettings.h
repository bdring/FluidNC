// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Settings and types shared between the WiFi and Ethernet network modules
// (WifiConfig.cpp and EthConfig.cpp).  This module must init() before either
// of those, so it is registered at a lower init_priority.

#pragma once

#include "Settings.h"
#include <string>

namespace WebUI {
    // Selects which network module actually brings up an interface.
    // Both WifiConfig and EthConfig check this before starting their
    // respective hardware, so only one of them is ever active at a time.
    enum NetworkType {
        NetworkTypeWiFi     = 0,
        NetworkTypeEthernet = 1,
    };

    extern EnumSetting* _network_type;
    NetworkType         networkType();

    // True if the currently-selected network interface ($network/type) is
    // turned on, regardless of link/IP state. Services like TelnetServer
    // and WebUIServer should gate startup on this instead of checking
    // WiFi.getMode() directly, so they work unmodified regardless of
    // whether WiFi or Ethernet is the active interface.
    bool networkEnabled();

    // True if the currently-selected interface currently has a usable IP
    // address / active connection. For logic that cares about live
    // connectivity, not just "the interface is configured on."
    bool networkConnected();

    // Hostname is meaningful regardless of which interface is active, so
    // it is shared rather than duplicated as e.g. Ethernet/Hostname.
    class HostnameSetting : public StringSetting {
    public:
        HostnameSetting(const char* description, const char* grblName, const char* name, const char* defVal);
        Error setStringValue(std::string_view s) override;
    };
    extern HostnameSetting* _hostname;
}
