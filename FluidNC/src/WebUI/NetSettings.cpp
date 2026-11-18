// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "NetSettings.h"
#include "Module.h"
#include "Config.h"
#include "Machine/MachineConfig.h"

#include <WiFi.h>
#if MAX_N_ETH
#    include <ETH.h>
#endif

#include <cctype>

namespace WebUI {
    EnumSetting*     _network_type = nullptr;
    HostnameSetting* _hostname     = nullptr;

    static constexpr int MAX_HOSTNAME_LENGTH = 32;
    static constexpr int MIN_HOSTNAME_LENGTH = 1;

    static const enum_opt_t networkTypeOptions = {
        { "WiFi", NetworkTypeWiFi },
        { "Ethernet", NetworkTypeEthernet },
    };

    // Create on first use: module init order is not guaranteed, and EthConfig
    // needs the network type before deciding whether to start the PHY.
    static void ensureSettings() {
        if (_network_type == nullptr) {
            _hostname = new HostnameSetting("Hostname", "ESP112", "Hostname", "fluidnc");
#if defined(CONFIG_IDF_TARGET_ESP32P4) && MAX_N_ETH
            // P4 boards have no WiFi radio; start on Ethernet.
            constexpr int defaultNetworkType = NetworkTypeEthernet;
#else
            constexpr int defaultNetworkType = NetworkTypeWiFi;
#endif
            _network_type = new EnumSetting("Network type", WEBSET, WA, NULL, "network/type", defaultNetworkType, &networkTypeOptions);
        }
    }

    NetworkType networkType() {
        ensureSettings();
        return NetworkType(_network_type->get());
    }

    bool networkEnabled() {
        switch (networkType()) {
            case NetworkTypeEthernet:
#if MAX_N_ETH
                return config->_ethernet != nullptr;
#else
                return false;
#endif
            case NetworkTypeWiFi:
            default:
                return WiFi.getMode() != WIFI_OFF;
        }
    }

    bool networkConnected() {
        switch (networkType()) {
            case NetworkTypeEthernet:
#if MAX_N_ETH
                return config->_ethernet && config->_ethernet->config_ok && ETH.hasIP();
#else
                return false;
#endif
            case NetworkTypeWiFi:
            default:
                return WiFi.status() == WL_CONNECTED;
        }
    }

    bool networkReady() {
        switch (networkType()) {
            case NetworkTypeEthernet:
#if MAX_N_ETH
                return config->_ethernet && config->_ethernet->config_ok;
#else
                return false;
#endif
            case NetworkTypeWiFi:
            default:
                return WiFi.getMode() == WIFI_STA;
        }
    }

    HostnameSetting::HostnameSetting(const char* description, const char* grblName, const char* name, const char* defVal) :
        StringSetting(description, WEBSET, WA, grblName, name, defVal, MIN_HOSTNAME_LENGTH, MAX_HOSTNAME_LENGTH) {
        load();
    }
    Error HostnameSetting::setStringValue(std::string_view s) {
        // Hostname strings may contain only letters, digits and -
        for (auto const& c : s) {
            if (c == ' ' || !(isdigit(c) || isalpha(c) || c == '-')) {
                return Error::InvalidValue;
            }
        }
        return StringSetting::setStringValue(s);
    }

    class NetSettingsModule : public Module {
    public:
        NetSettingsModule(const char* name) : Module(name) {}
        void init() override { ensureSettings(); }
    };

    // Register before the WiFi/Ethernet modules (105/106).
    ModuleFactory::InstanceBuilder<NetSettingsModule> __attribute__((init_priority(104))) net_settings_module("netsettings", true);
}
