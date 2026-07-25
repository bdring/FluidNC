// Copyright (c) 2026 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Ethernet network module. Mirrors WifiConfig.cpp's structure and settings
// naming where it makes sense, but the physical PHY (pins, chip select,
// phy_type) is configured in config.yaml as Machine::EthPhy (config->_ethernet),
// not via NVS settings -- see the discussion in Machine/EthPhy.h.
//
// $network/type selects whether WiFi or Ethernet actually brings up an
// interface; both modules create their settings unconditionally so that $
// commands still work regardless of which is selected, but only the
// selected one starts hardware.

#include "Config.h"
#if MAX_N_ETH

#    include "Settings.h"
#    include "Machine/MachineConfig.h"

#    include "Channel.h"
#    include "Error.h"
#    include "Module.h"
#    include "Authentication.h"

#    include "Main.h"

#    include "WebUIServer.h"
#    include "TelnetServer.h"
#    include "NotificationsService.h"

#    include "NetSettings.h"
#    include "Driver/localfs.h"
#    include "Driver/watchdog.h"  // feed_watchdog

#    include <ETH.h>
#    include <string>
#    include <cstring>

namespace WebUI {

    static constexpr int NET_DHCP_MODE   = 0;
    static constexpr int NET_STATIC_MODE = 1;

    static const enum_opt_t ethIpModeOptions = {
        { "DHCP", NET_DHCP_MODE },
        { "Static", NET_STATIC_MODE },
    };

    static const char* NULL_IP = "0.0.0.0";

    static EnumSetting*   _eth_ip_mode;
    static IPaddrSetting* _eth_ip;
    static IPaddrSetting* _eth_gateway;
    static IPaddrSetting* _eth_netmask;

    std::string ethIp() { return IP_string(ETH.localIP()); }

    class EthConfig : public Module {
    private:
        static Error showSetEthParams(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            if (*parameter == '\0') {
                log_stream(out,
                           "IP:" << _eth_ip->getStringValue() << " GW:" << _eth_gateway->getStringValue()
                                 << " MSK:" << _eth_netmask->getStringValue());
                return Error::Ok;
            }
            std::string gateway, netmask, ip;
            if (!(get_param(parameter, "GW", gateway) && get_param(parameter, "MSK", netmask) && get_param(parameter, "IP", ip))) {
                return Error::InvalidValue;
            }
            Error err = _eth_ip->setStringValue(ip);
            if (err == Error::Ok) {
                err = _eth_netmask->setStringValue(netmask);
            }
            if (err == Error::Ok) {
                err = _eth_gateway->setStringValue(gateway);
            }
            return err;
        }

        static void reportStatus(Channel& out) {
            if (!isOn()) {
                log_string(out, "Ethernet: Off");
                return;
            }
            log_stream(out, "Available Size for LocalFS: " << formatBytes(localfs_size()));
            log_stream(out, "Web port: " << WebUI_Server::port());
            log_stream(out, "Hostname: " << ETH.getHostname());
            log_stream(out, "MAC: " << ETH.macAddress().c_str());
            log_stream(out, "Link: " << (ETH.linkUp() ? "Up" : "Down"));
            if (ETH.linkUp()) {
                log_stream(out, "IP Mode: " << _eth_ip_mode->getStringValue());
                log_stream(out, "IP: " << IP_string(ETH.localIP()));
                log_stream(out, "Gateway: " << IP_string(ETH.gatewayIP()));
                log_stream(out, "Mask: " << IP_string(ETH.subnetMask()));
                log_stream(out, "DNS: " << IP_string(ETH.dnsIP()));
            }

            LogStream s(out, "Notifications: ");
            s << (NotificationsService::started() ? "Enabled" : "Disabled");
            if (NotificationsService::started()) {
                s << "(" << NotificationsService::getTypeString() << ")";
            }
        }

        void status_report(Channel& out) override { reportStatus(out); }

        void wifi_stats(JSONencoder& j) override {
            if (!isOn()) {
                j.id_value_object("Current Network Mode", "Ethernet Off");
                return;
            }
            j.id_value_object("Current Network Mode", std::string("Ethernet (") + ETH.macAddress().c_str() + ")");
            j.id_value_object("Available Size for LocalFS", formatBytes(localfs_size()));
            j.id_value_object("Web port", WebUI_Server::port());
            j.id_value_object("Hostname", ETH.getHostname());
            j.id_value_object("Link", ETH.linkUp() ? "Up" : "Down");
            if (ETH.linkUp()) {
                j.id_value_object("IP Mode", _eth_ip_mode->getStringValue());
                j.id_value_object("IP", IP_string(ETH.localIP()));
                j.id_value_object("Gateway", IP_string(ETH.gatewayIP()));
                j.id_value_object("Mask", IP_string(ETH.subnetMask()));
                j.id_value_object("DNS", IP_string(ETH.dnsIP()));
            }
        }

        static Error showEthStatus(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            (void)parameter;
            (void)auth_level;
            reportStatus(out);
            return Error::Ok;
        }

        static bool isOn() { return config->_ethernet && config->_ethernet->config_ok; }

        // Manual trigger for EthPhy::init(), independent of $network/type,
        // so the PHY bring-up sequence can be traced (e.g. with a logic
        // analyzer) without needing Ethernet selected as the active network
        // type and without the reboot-loop risk of running it automatically
        // at boot before the wiring is known good.
        static Error initEth(const char* parameter, AuthenticationLevel auth_level, Channel& out) {
            (void)parameter;
            (void)auth_level;
            if (!config->_ethernet) {
                log_stream(out, "Ethernet is not configured (no ethernet: section)");
                return Error::InvalidStatement;
            }
            log_stream(out, "Initializing Ethernet PHY...");
            bool ok = config->_ethernet->init();
            log_stream(out, "Ethernet PHY init " << (ok ? "succeeded" : "failed"));
            return ok ? Error::Ok : Error::InvalidStatement;
        }

        static bool StartEth() {
            if (!config->_ethernet) {
                log_info("Ethernet is not configured (no ethernet: section)");
                return false;
            }

            log_info("Hostname is " << _hostname->get());

            if (!config->_ethernet->init()) {
                return false;
            }

            ETH.setHostname(_hostname->get());

            if (_eth_ip_mode->get() == NET_STATIC_MODE) {
                uint32_t ip      = (uint32_t)_eth_ip->get();
                uint32_t gateway = (uint32_t)_eth_gateway->get();
                uint32_t netmask = (uint32_t)_eth_netmask->get();
                log_info("Using static Ethernet config IP=" << IP_string(ip) << " GW=" << IP_string(gateway)
                                                              << " MASK=" << IP_string(netmask));
                // Use the gateway as the DNS forwarder, matching WifiConfig's STA behavior.
                if (!ETH.config(IPAddress(ip), IPAddress(gateway), IPAddress(netmask), IPAddress(gateway))) {
                    log_error("Failed to apply static Ethernet config");
                    return false;
                }
            }

            // Wait briefly for link up / DHCP lease, mirroring WifiConfig::ConnectSTA2AP
            // but without WiFi's association-retry complexity -- a wired link either
            // comes up quickly or it's not plugged in.
            // feed_watchdog() is a no-op if this task isn't TWDT-subscribed (the
            // normal case -- see platform_preinit()), but costs nothing and
            // protects against a multi-second blocking loop like this one
            // tripping the watchdog if that ever changes.
            for (int i = 0; i < 50 && !ETH.linkUp(); ++i) {
                feed_watchdog();
                delay_ms(100);
            }
            if (!ETH.linkUp()) {
                log_info("Ethernet link is down (cable unplugged?)");
                // Not fatal: the module stays "on" and will report link-down status;
                // it will come up automatically if a cable is connected later.
            } else {
                log_info("Ethernet link up");
                for (int i = 0; i < 50 && _eth_ip_mode->get() == NET_DHCP_MODE && ETH.localIP() == IPAddress((uint32_t)0); ++i) {
                    feed_watchdog();
                    delay_ms(100);
                }
                log_info("Ethernet IP is " << IP_string(ETH.localIP()));
            }
            return true;
        }

    public:
        EthConfig(const char* name) : Module(name) {}

        void init() override {
            _eth_ip_mode = new EnumSetting("Ethernet IP Mode", WEBSET, WA, NULL, "Ethernet/IPMode", NET_DHCP_MODE, &ethIpModeOptions);
            _eth_ip      = new IPaddrSetting("Ethernet Static IP", WEBSET, WA, NULL, "Ethernet/IP", NULL_IP);
            _eth_gateway = new IPaddrSetting("Ethernet Static Gateway", WEBSET, WA, NULL, "Ethernet/Gateway", NULL_IP);
            _eth_netmask = new IPaddrSetting("Ethernet Static Mask", WEBSET, WA, NULL, "Ethernet/Netmask", NULL_IP);

            new WebCommand(NULL, WEBCMD, WG, NULL, "Ethernet/Status", showEthStatus, anyState);
            new WebCommand("IP=ipaddress MSK=netmask GW=gateway", WEBCMD, WA, NULL, "Ethernet/Setup", showSetEthParams);
            new WebCommand(NULL, WEBCMD, WA, "EI", "Ethernet/Init", initEth, anyState);

            if (networkType() != NetworkTypeEthernet) {
                log_info("Ethernet is disabled ($network/type is WiFi)");
                return;
            }

            if (StartEth()) {
                log_info("Ethernet on");
            } else {
                log_info("Ethernet off");
            }
        }

        void deinit() override {
            if (isOn()) {
                ETH.end();
            }
        }

        void poll() override {}

        bool is_radio() override { return false; }

        ~EthConfig() { deinit(); }
    };

    ModuleFactory::InstanceBuilder<EthConfig> __attribute__((init_priority(106))) eth_module("ethernet", true);
}
#endif
