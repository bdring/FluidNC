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

    NetworkType networkType() { return _network_type ? NetworkType(_network_type->get()) : NetworkTypeWiFi; }

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
        void init() override {
            _hostname     = new HostnameSetting("Hostname", "ESP112", "Hostname", "fluidnc");
            _network_type = new EnumSetting("Network type", WEBSET, WA, NULL, "network/type", NetworkTypeWiFi, &networkTypeOptions);
        }
    };

    // init_priority must be lower than WiFi/Ethernet (105) so _hostname and
    // _network_type exist by the time those modules init().
    ModuleFactory::InstanceBuilder<NetSettingsModule> __attribute__((init_priority(104))) net_settings_module("netsettings", true);
}
