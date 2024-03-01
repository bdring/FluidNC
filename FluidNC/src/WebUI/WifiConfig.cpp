// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WifiConfig.h"

#include "../Settings.h"
#include "../Machine/MachineConfig.h"
#include <sstream>
#include <iomanip>

WebUI::WiFiConfig wifi_config __attribute__((init_priority(109)));

#ifdef ENABLE_WIFI
#    include "../Config.h"
#    include "../Main.h"
#    include "Commands.h"      // COMMANDS
#    include "WifiServices.h"  // wifi_services.start() etc.
#    include "WebSettings.h"   // get_param()

#    include "WebServer.h"             // webServer.port()
#    include "TelnetServer.h"          // telnetServer
#    include "NotificationsService.h"  // notificationsservice

#    include <WiFi.h>
#    include <esp_wifi.h>
#    include "Driver/localfs.h"
#    include <cstring>

#    include <esp_ota_ops.h>

namespace WebUI {
    enum WiFiStartupMode {
        WiFiOff = 0,
        WiFiSTA,
        WiFiAP,
        WiFiFallback,  // Try STA and fall back to AP if STA fails
    };

    enum_opt_t wifiModeOptions = {
        { "Off", WiFiOff },
        { "STA", WiFiSTA },
        { "AP", WiFiAP },
        { "STA>AP", WiFiFallback },
    };

    enum WiFiContry {
        WiFiCountry01 = 0,  // country "01" is the safest set of settings which complies with all regulatory domains
        WiFiCountryAT,
        WiFiCountryAU,
        WiFiCountryBE,
        WiFiCountryBG,
        WiFiCountryBR,
        WiFiCountryCA,
        WiFiCountryCH,
        WiFiCountryCN,
        WiFiCountryCY,
        WiFiCountryCZ,
        WiFiCountryDE,
        WiFiCountryDK,
        WiFiCountryEE,
        WiFiCountryES,
        WiFiCountryFI,
        WiFiCountryFR,
        WiFiCountryGB,
        WiFiCountryGR,
        WiFiCountryHK,
        WiFiCountryHR,
        WiFiCountryHU,
        WiFiCountryIE,
        WiFiCountryIN,
        WiFiCountryIS,
        WiFiCountryIT,
        WiFiCountryJP,
        WiFiCountryKR,
        WiFiCountryLI,
        WiFiCountryLT,
        WiFiCountryLU,
        WiFiCountryLV,
        WiFiCountryMT,
        WiFiCountryMX,
        WiFiCountryNL,
        WiFiCountryNO,
        WiFiCountryNZ,
        WiFiCountryPL,
        WiFiCountryPT,
        WiFiCountryRO,
        WiFiCountrySE,
        WiFiCountrySI,
        WiFiCountrySK,
        WiFiCountryTW,
        WiFiCountryUS,
    };

    enum_opt_t wifiContryOptions = {
        { "01", WiFiCountry01 }, { "AT", WiFiCountryAT }, { "AU", WiFiCountryAU }, { "BE", WiFiCountryBE }, { "BG", WiFiCountryBG },
        { "BR", WiFiCountryBR }, { "CA", WiFiCountryCA }, { "CH", WiFiCountryCH }, { "CN", WiFiCountryCN }, { "CY", WiFiCountryCY },
        { "CZ", WiFiCountryCZ }, { "DE", WiFiCountryDE }, { "DK", WiFiCountryDK }, { "EE", WiFiCountryEE }, { "ES", WiFiCountryES },
        { "FI", WiFiCountryFI }, { "FR", WiFiCountryFR }, { "GB", WiFiCountryGB }, { "GR", WiFiCountryGR }, { "HK", WiFiCountryHK },
        { "HR", WiFiCountryHR }, { "HU", WiFiCountryHU }, { "IE", WiFiCountryIE }, { "IN", WiFiCountryIN }, { "IS", WiFiCountryIS },
        { "IT", WiFiCountryIT }, { "JP", WiFiCountryJP }, { "KR", WiFiCountryKR }, { "LI", WiFiCountryLI }, { "LT", WiFiCountryLT },
        { "LU", WiFiCountryLU }, { "LV", WiFiCountryLV }, { "MT", WiFiCountryMT }, { "MX", WiFiCountryMX }, { "NL", WiFiCountryNL },
        { "NO", WiFiCountryNO }, { "NZ", WiFiCountryNZ }, { "PL", WiFiCountryPL }, { "PT", WiFiCountryPT }, { "RO", WiFiCountryRO },
        { "SE", WiFiCountrySE }, { "SI", WiFiCountrySI }, { "SK", WiFiCountrySK }, { "TW", WiFiCountryTW }, { "US", WiFiCountryUS },
    };

    EnumSetting* wifi_mode;

    StringSetting* wifi_sta_ssid;
    StringSetting* wifi_sta_password;

    EnumSetting*   wifi_fast_scan;
    EnumSetting*   wifi_sta_min_security;
    EnumSetting*   wifi_sta_mode;
    IPaddrSetting* wifi_sta_ip;
    IPaddrSetting* wifi_sta_gateway;
    IPaddrSetting* wifi_sta_netmask;
    EnumSetting*   wifi_sta_ssdp;

    StringSetting*   wifi_ap_ssid;
    PasswordSetting* wifi_ap_password;
    EnumSetting*     wifi_ap_country;

    IPaddrSetting* wifi_ap_ip;

    IntSetting* wifi_ap_channel;

    HostnameSetting* wifi_hostname;

    enum_opt_t staModeOptions = {
        { "DHCP", DHCP_MODE },
        { "Static", STATIC_MODE },
    };

    enum_opt_t staSecurityOptions = {
        { "OPEN", WIFI_AUTH_OPEN },
        { "WEP", WIFI_AUTH_WEP },
        { "WPA-PSK", WIFI_AUTH_WPA_PSK },
        { "WPA2-PSK", WIFI_AUTH_WPA2_PSK },
        { "WPA-WPA2-PSK", WIFI_AUTH_WPA_WPA2_PSK },
        { "WPA2-ENTERPRISE", WIFI_AUTH_WPA2_ENTERPRISE },
    };

    static void print_mac(Channel& out, const char* prefix, const char* mac) { log_stream(out, prefix << " (" << mac << ")"); }

    static Error showIP(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
        log_stream(out, parameter << IP_string(WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP()));
        return Error::Ok;
    }

    static Error showSetStaParams(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP103
        if (*parameter == '\0') {
            log_stream(out,
                       "IP:" << wifi_sta_ip->getStringValue() << " GW:" << wifi_sta_gateway->getStringValue()
                             << " MSK:" << wifi_sta_netmask->getStringValue());
            return Error::Ok;
        }
        std::string gateway, netmask, ip;
        if (!(get_param(parameter, "GW", gateway) && get_param(parameter, "MSK", netmask) && get_param(parameter, "IP", ip))) {
            return Error::InvalidValue;
        }

        Error err = wifi_sta_ip->setStringValue(ip);
        if (err == Error::Ok) {
            err = wifi_sta_netmask->setStringValue(netmask);
        }
        if (err == Error::Ok) {
            err = wifi_sta_gateway->setStringValue(gateway);
        }
        return err;
    }

    void addIdValueObject(JSONencoder& j, const char* id, const char* value) {
        j.begin_object();
        j.member("id", id);
        j.member("value", value);
        j.end_object();
    }

    void addIdValueObject(JSONencoder& j, const char* id, const std::string& value) {
        j.begin_object();
        j.member("id", id);
        j.member("value", value);
        j.end_object();
    }

    void addIdValueObject(JSONencoder& j, const char* id, int value) {
        j.begin_object();
        j.member("id", id);
        j.member("value", std::to_string(value));
        j.end_object();
    }

    void WiFiConfig::addWifiStatsToArray(JSONencoder& j) {
        addIdValueObject(j, "Sleep mode", WiFi.getSleep() ? "Modem" : "None");
        int mode = WiFi.getMode();
        if (mode != WIFI_MODE_NULL) {
            //Is OTA available ?
            size_t flashsize = 0;
            if (esp_ota_get_running_partition()) {
                const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                if (partition) {
                    flashsize = partition->size;
                }
            }
            addIdValueObject(j, "Available Size for update", formatBytes(flashsize));
            addIdValueObject(j, "Available Size for LocalFS", formatBytes(localfs_size()));
            addIdValueObject(j, "Web port", webServer.port());
            addIdValueObject(j, "Data port", telnetServer.port());
            addIdValueObject(j, "Hostname", wifi_config.Hostname());
        }

        switch (mode) {
            case WIFI_STA:

                addIdValueObject(j, "Current WiFi Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");

                if (WiFi.isConnected()) {  //in theory no need but ...
                    addIdValueObject(j, "Connected to", WiFi.SSID().c_str());
                    addIdValueObject(j, "Signal", std::string("") + std::to_string(wifi_config.getSignal(WiFi.RSSI())) + "%");

                    uint8_t PhyMode;
                    esp_wifi_get_protocol(WIFI_IF_STA, &PhyMode);
                    const char* modeName;
                    switch (PhyMode) {
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N:
                            modeName = "11n";
                            break;
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G:
                            modeName = "11g";
                            break;
                        case WIFI_PROTOCOL_11B:
                            modeName = "11b";
                            break;
                        default:
                            modeName = "???";
                    }

                    addIdValueObject(j, "Phy Mode: ", modeName);
                    addIdValueObject(j, "Channel: ", WiFi.channel());

                    tcpip_adapter_dhcp_status_t dhcp_status;
                    tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &dhcp_status);
                    addIdValueObject(j, "IP Mode: ", (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "DHCP" : "Static"));
                    addIdValueObject(j, "IP: ", IP_string(WiFi.localIP()));
                    addIdValueObject(j, "Gateway: ", IP_string(WiFi.gatewayIP()));
                    addIdValueObject(j, "Mask: ", IP_string(WiFi.subnetMask()));
                    addIdValueObject(j, "DNS: ", IP_string(WiFi.dnsIP()));

                }  //this is web command so connection => no command
                addIdValueObject(j, "Disabled Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                break;
            case WIFI_AP:
                addIdValueObject(j, "Current WiFi Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                wifi_config_t  conf;
                wifi_country_t country;
                esp_wifi_get_config(WIFI_IF_AP, &conf);
                esp_wifi_get_country(&country);
                addIdValueObject(j, "SSID: ", (const char*)conf.ap.ssid);
                addIdValueObject(j, "Visible: ", (conf.ap.ssid_hidden == 0 ? "Yes" : "No"));
                addIdValueObject(j,
                                 "Radio country set: ",
                                 std::string("") + country.cc + " (channels " + std::to_string(country.schan) + "-" +
                                     std::to_string((country.schan + country.nchan - 1)) + ", max power " +
                                     std::to_string(country.max_tx_power) + "dBm)");

                const char* mode;
                switch (conf.ap.authmode) {
                    case WIFI_AUTH_OPEN:
                        mode = "None";
                        break;
                    case WIFI_AUTH_WEP:
                        mode = "WEP";
                        break;
                    case WIFI_AUTH_WPA_PSK:
                        mode = "WPA-PSK";
                        break;
                    case WIFI_AUTH_WPA2_PSK:
                        mode = "WPA2-PSK";
                        break;
                    case WIFI_AUTH_WPA_WPA2_PSK:
                        mode = "WPA-WPA2-PSK";
                        break;
                    default:
                        mode = "WPA/WPA2";
                }

                addIdValueObject(j, "Authentication", mode);
                addIdValueObject(j, "Max Connections", conf.ap.max_connection);

                tcpip_adapter_dhcp_status_t dhcp_status;
                tcpip_adapter_dhcps_get_status(TCPIP_ADAPTER_IF_AP, &dhcp_status);
                addIdValueObject(j, "DHCP Server", (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "Started" : "Stopped"));

                addIdValueObject(j, "IP", IP_string(WiFi.softAPIP()));

                tcpip_adapter_ip_info_t ip_AP;
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_AP);
                addIdValueObject(j, "Gateway", IP_string(IPAddress(ip_AP.gw.addr)));
                addIdValueObject(j, "Mask", IP_string(IPAddress(ip_AP.netmask.addr)));

                wifi_sta_list_t          station;
                tcpip_adapter_sta_list_t tcpip_sta_list;
                esp_wifi_ap_get_sta_list(&station);
                tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                addIdValueObject(j, "Connected channels", station.num);

                for (int i = 0; i < station.num; i++) {
                    addIdValueObject(j,
                                     "",
                                     std::string("") + wifi_config.mac2str(tcpip_sta_list.sta[i].mac) + " " +
                                         IP_string(IPAddress(tcpip_sta_list.sta[i].ip.addr)));
                }
                addIdValueObject(j, "Disabled Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                break;
            case WIFI_AP_STA:  //we should not be in this state but just in case ....
                addIdValueObject(j, "Mixed", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                addIdValueObject(j, "Mixed", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                break;
            default:  //we should not be there if no wifi ....

                addIdValueObject(j, "Current WiFi Mode", "Off");
                break;
        }
    }

    void WiFiConfig::showWifiStats(Channel& out) {
        log_stream(out, "Sleep mode: " << (WiFi.getSleep() ? "Modem" : "None"));
        int mode = WiFi.getMode();
        if (mode != WIFI_MODE_NULL) {
            //Is OTA available ?
            size_t flashsize = 0;
            if (esp_ota_get_running_partition()) {
                const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                if (partition) {
                    flashsize = partition->size;
                }
            }
            log_stream(out, "Available Size for update: " << formatBytes(flashsize));
            log_stream(out, "Available Size for LocalFS: " << formatBytes(localfs_size()));
            log_stream(out, "Web port: " << webServer.port());
            log_stream(out, "Data port: " << telnetServer.port());
            log_stream(out, "Hostname: " << wifi_config.Hostname());
        }

        switch (mode) {
            case WIFI_STA:
                print_mac(out, "Current WiFi Mode: STA", WiFi.macAddress().c_str());

                if (WiFi.isConnected()) {  //in theory no need but ...
                    log_stream(out, "Connected to: " << WiFi.SSID().c_str());
                    log_stream(out, "Signal: " << wifi_config.getSignal(WiFi.RSSI()) << "%");

                    uint8_t PhyMode;
                    esp_wifi_get_protocol(WIFI_IF_STA, &PhyMode);
                    const char* phyModeName;
                    switch (PhyMode) {
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N:
                            phyModeName = "11n";
                            break;
                        case WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G:
                            phyModeName = "11g";
                            break;
                        case WIFI_PROTOCOL_11B:
                            phyModeName = "11b";
                            break;
                        default:
                            phyModeName = "???";
                    }
                    log_stream(out, "Phy Mode: " << phyModeName);
                    log_stream(out, "Channel: " << WiFi.channel());

                    tcpip_adapter_dhcp_status_t dhcp_status;
                    tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &dhcp_status);
                    log_stream(out, "IP Mode: " << (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "DHCP" : "Static"));
                    log_stream(out, "IP: " << IP_string(WiFi.localIP()));
                    log_stream(out, "Gateway: " << IP_string(WiFi.gatewayIP()));
                    log_stream(out, "Mask: " << IP_string(WiFi.subnetMask()));
                    log_stream(out, "DNS: " << IP_string(WiFi.dnsIP()));

                }  //this is web command so connection => no command
                print_mac(out, "Disabled Mode: AP", WiFi.softAPmacAddress().c_str());
                break;
            case WIFI_AP:
                print_mac(out, "Current WiFi Mode: AP", WiFi.softAPmacAddress().c_str());

                wifi_config_t  conf;
                wifi_country_t country;
                esp_wifi_get_config(WIFI_IF_AP, &conf);
                esp_wifi_get_country(&country);
                log_stream(out, "SSID: " << (const char*)conf.ap.ssid);
                log_stream(out, "Visible: " << (conf.ap.ssid_hidden == 0 ? "Yes" : "No"));
                log_stream(out,
                           "Radio country set: " << country.cc[0] << country.cc[1] << " (channels " << country.schan << "-"
                                                 << (country.schan + country.nchan - 1) << ", max power " << country.max_tx_power << "dBm)");

                const char* mode;
                switch (conf.ap.authmode) {
                    case WIFI_AUTH_OPEN:
                        mode = "None";
                        break;
                    case WIFI_AUTH_WEP:
                        mode = "WEP";
                        break;
                    case WIFI_AUTH_WPA_PSK:
                        mode = "WPA-PSK";
                        break;
                    case WIFI_AUTH_WPA2_PSK:
                        mode = "WPA2-PSK";
                        break;
                    case WIFI_AUTH_WPA_WPA2_PSK:
                        mode = "WPA-WPA2-PSK";
                        break;
                    default:
                        mode = "WPA/WPA2";
                }

                log_stream(out, "Authentication: " << mode);
                log_stream(out, "Max Connections: " << conf.ap.max_connection);

                tcpip_adapter_dhcp_status_t dhcp_status;
                tcpip_adapter_dhcps_get_status(TCPIP_ADAPTER_IF_AP, &dhcp_status);
                log_stream(out, "DHCP Server: " << (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "Started" : "Stopped"));

                log_stream(out, "IP: " << IP_string(WiFi.softAPIP()));

                tcpip_adapter_ip_info_t ip_AP;
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_AP);
                log_stream(out, "Gateway: " << IP_string(IPAddress(ip_AP.gw.addr)));
                log_stream(out, "Mask: " << IP_string(IPAddress(ip_AP.netmask.addr)));

                wifi_sta_list_t          station;
                tcpip_adapter_sta_list_t tcpip_sta_list;
                esp_wifi_ap_get_sta_list(&station);
                tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                log_stream(out, "Connected channels: " << station.num);

                for (int i = 0; i < station.num; i++) {
                    log_stream(
                        out, wifi_config.mac2str(tcpip_sta_list.sta[i].mac) << " " << IP_string(IPAddress(tcpip_sta_list.sta[i].ip.addr)));
                }
                print_mac(out, "Disabled Mode: STA", WiFi.macAddress().c_str());
                break;
            case WIFI_AP_STA:  //we should not be in this state but just in case ....
                log_string(out, "");

                print_mac(out, "Mixed: STA", WiFi.macAddress().c_str());
                print_mac(out, "Mixed: AP", WiFi.softAPmacAddress().c_str());
                break;
            default:  //we should not be there if no wifi ....

                log_string(out, "Current WiFi Mode: Off");
                break;
        }

        LogStream s(out, "Notifications: ");
        s << (notificationsService.started() ? "Enabled" : "Disabled");
        if (notificationsService.started()) {
            s << "(" << notificationsService.getTypeString() << ")";
        }
    }

    const char* WiFiConfig::modeName() {
        switch (WiFi.getMode()) {
            case WIFI_MODE_NULL:
                return "None";
            case WIFI_STA:
                return "STA";
            case WIFI_AP:
                return "AP";
            default:
                return "?";
        }
    }

    std::string WiFiConfig::_hostname("");
    bool        WiFiConfig::_events_registered = false;

    WiFiConfig::WiFiConfig() {
        new WebCommand(NULL, WEBCMD, WU, "ESP410", "WiFi/ListAPs", listAPs);

        wifi_hostname = new HostnameSetting("Hostname", "ESP112", "Hostname", DEFAULT_HOSTNAME);

        wifi_ap_channel  = new IntSetting("AP Channel", WEBSET, WA, "ESP108", "AP/Channel", DEFAULT_AP_CHANNEL, MIN_CHANNEL, MAX_CHANNEL);
        wifi_ap_ip       = new IPaddrSetting("AP Static IP", WEBSET, WA, "ESP107", "AP/IP", DEFAULT_AP_IP);
        wifi_ap_password = new PasswordSetting("AP Password", "ESP106", "AP/Password", DEFAULT_AP_PWD);
        wifi_ap_ssid     = new StringSetting("AP SSID", WEBSET, WA, "ESP105", "AP/SSID", DEFAULT_AP_SSID, MIN_SSID_LENGTH, MAX_SSID_LENGTH);
        wifi_ap_country  = new EnumSetting("AP regulatory domain", WEBSET, WA, NULL, "AP/Country", WiFiCountry01, &wifiContryOptions);
        wifi_sta_ssdp = new EnumSetting("SSDP and mDNS enable", WEBSET, WA, NULL, "Sta/SSDP/Enable", DEFAULT_STA_SSDP_ENABLED, &onoffOptions);
        wifi_sta_netmask = new IPaddrSetting("Station Static Mask", WEBSET, WA, NULL, "Sta/Netmask", DEFAULT_STA_MK);
        wifi_sta_gateway = new IPaddrSetting("Station Static Gateway", WEBSET, WA, NULL, "Sta/Gateway", DEFAULT_STA_GW);
        wifi_sta_ip      = new IPaddrSetting("Station Static IP", WEBSET, WA, NULL, "Sta/IP", DEFAULT_STA_IP);
        wifi_sta_mode    = new EnumSetting("Station IP Mode", WEBSET, WA, "ESP102", "Sta/IPMode", DEFAULT_STA_IP_MODE, &staModeOptions);
        wifi_fast_scan   = new EnumSetting("WiFi Fast Scan", WEBSET, WA, NULL, "WiFi/FastScan", 0, &onoffOptions);
        wifi_sta_min_security =
            new EnumSetting("Station IP Mode", WEBSET, WA, NULL, "Sta/MinSecurity", DEFAULT_STA_MIN_SECURITY, &staSecurityOptions);
        wifi_sta_password = new PasswordSetting("Station Password", "ESP101", "Sta/Password", DEFAULT_STA_PWD);
        wifi_sta_ssid =
            new StringSetting("Station SSID", WEBSET, WA, "ESP100", "Sta/SSID", DEFAULT_STA_SSID, MIN_SSID_LENGTH, MAX_SSID_LENGTH);

        wifi_mode = new EnumSetting("WiFi mode", WEBSET, WA, "ESP116", "WiFi/Mode", WiFiFallback, &wifiModeOptions);

        new WebCommand(NULL, WEBCMD, WG, "ESP111", "System/IP", showIP);
        new WebCommand("IP=ipaddress MSK=netmask GW=gateway", WEBCMD, WA, "ESP103", "Sta/Setup", showSetStaParams);
    }

    //just simple helper to convert mac address to string
    char* WiFiConfig::mac2str(uint8_t mac[8]) {
        static char macstr[18];
        if (0 > sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5])) {
            strcpy(macstr, "00:00:00:00:00:00");
        }
        return macstr;
    }

    std::string WiFiConfig::webInfo() {
        std::string s;
        s += " # webcommunication: Sync: ";
        s += std::to_string(webServer.port() + 1) + ":";
        switch (WiFi.getMode()) {
            case WIFI_MODE_AP:
                s += IP_string(WiFi.softAPIP());
                break;
            case WIFI_MODE_STA:
                s += IP_string(WiFi.localIP());
                break;
            case WIFI_MODE_APSTA:
                s += IP_string(WiFi.softAPIP());
                break;
            default:
                s += "0.0.0.0";
                break;
        }
        s += " # hostname:" + wifi_config.Hostname();
        if (WiFi.getMode() == WIFI_AP) {
            s += "(AP mode)";
        }
        return s;
    }

    std::string WiFiConfig::station_info() {
        std::string result;

        if ((WiFi.getMode() == WIFI_MODE_STA) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            result += "Mode=STA:SSID=";
            result += WiFi.SSID().c_str();
            result += ":Status=";
            result += (WiFi.status() == WL_CONNECTED) ? "Connected" : "Not connected";
            result += ":IP=";
            result += IP_string(WiFi.localIP());
            result += ":MAC=";
            std::string mac(WiFi.macAddress().c_str());
            std::replace(mac.begin(), mac.end(), ':', '-');
            result += mac;
        }
        return result;
    }

    std::string WiFiConfig::ap_info() {
        std::string result;

        if ((WiFi.getMode() == WIFI_MODE_AP) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            if (WiFi.getMode() == WIFI_MODE_APSTA) {
                result += "]\n[MSG:";
            }
            result += "Mode=AP:SSID=";
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            result += (const char*)conf.ap.ssid;
            result += ":IP=";
            result += IP_string(WiFi.softAPIP());
            result += ":MAC=";
            std::string mac(WiFi.softAPmacAddress().c_str());
            std::replace(mac.begin(), mac.end(), ':', '-');
            result += mac;
        }
        return result;
    }

    /**
     * WiFi events
     * SYSTEM_EVENT_WIFI_READY               < ESP32 WiFi ready
     * SYSTEM_EVENT_SCAN_DONE                < ESP32 finish scanning AP
     * SYSTEM_EVENT_STA_START                < ESP32 station start
     * SYSTEM_EVENT_STA_STOP                 < ESP32 station stop
     * SYSTEM_EVENT_STA_CONNECTED            < ESP32 station connected to AP
     * SYSTEM_EVENT_STA_DISCONNECTED         < ESP32 station disconnected from AP
     * SYSTEM_EVENT_STA_AUTHMODE_CHANGE      < the auth mode of AP connected by ESP32 station changed
     * SYSTEM_EVENT_STA_GOT_IP               < ESP32 station got IP from connected AP
     * SYSTEM_EVENT_STA_LOST_IP              < ESP32 station lost IP and the IP is reset to 0
     * SYSTEM_EVENT_STA_WPS_ER_SUCCESS       < ESP32 station wps succeeds in enrollee mode
     * SYSTEM_EVENT_STA_WPS_ER_FAILED        < ESP32 station wps fails in enrollee mode
     * SYSTEM_EVENT_STA_WPS_ER_TIMEOUT       < ESP32 station wps timeout in enrollee mode
     * SYSTEM_EVENT_STA_WPS_ER_PIN           < ESP32 station wps pin code in enrollee mode
     * SYSTEM_EVENT_AP_START                 < ESP32 soft-AP start
     * SYSTEM_EVENT_AP_STOP                  < ESP32 soft-AP stop
     * SYSTEM_EVENT_AP_STACONNECTED          < a station connected to ESP32 soft-AP
     * SYSTEM_EVENT_AP_STADISCONNECTED       < a station disconnected from ESP32 soft-AP
     * SYSTEM_EVENT_AP_PROBEREQRECVED        < Receive probe request packet in soft-AP interface
     * SYSTEM_EVENT_GOT_IP6                  < ESP32 station or ap or ethernet interface v6IP addr is preferred
     * SYSTEM_EVENT_ETH_START                < ESP32 ethernet start
     * SYSTEM_EVENT_ETH_STOP                 < ESP32 ethernet stop
     * SYSTEM_EVENT_ETH_CONNECTED            < ESP32 ethernet phy link up
     * SYSTEM_EVENT_ETH_DISCONNECTED         < ESP32 ethernet phy link down
     * SYSTEM_EVENT_ETH_GOT_IP               < ESP32 ethernet got IP from connected AP
     * SYSTEM_EVENT_MAX
     */

    void WiFiConfig::WiFiEvent(WiFiEvent_t event) {
        switch (event) {
            case SYSTEM_EVENT_STA_GOT_IP:
                break;
            case SYSTEM_EVENT_STA_DISCONNECTED:
                log_info("WiFi Disconnected");
                break;
            default:
                //log_info("WiFi event:" << event);
                break;
        }
    }

    /*
     * Get WiFi signal strength
     */
    int32_t WiFiConfig::getSignal(int32_t RSSI) {
        if (RSSI <= -100) {
            return 0;
        }
        if (RSSI >= -50) {
            return 100;
        }
        return 2 * (RSSI + 100);
    }

    /*
     * Connect client to AP
     */

    bool WiFiConfig::ConnectSTA2AP() {
        std::string msg, msg_out;
        uint8_t     dot = 0;
        for (size_t i = 0; i < 10; ++i) {
            switch (WiFi.status()) {
                case WL_NO_SSID_AVAIL:
                    log_info("No SSID");
                    return false;
                case WL_CONNECT_FAILED:
                    log_info("Connection failed");
                    return false;
                case WL_CONNECTED:
                    log_info("Connected - IP is " << IP_string(WiFi.localIP()));
                    return true;
                default:
                    if ((dot > 3) || (dot == 0)) {
                        dot     = 0;
                        msg_out = "Connecting";
                    }
                    msg_out += ".";
                    msg = msg_out;
                    dot++;
                    break;
            }
            log_info(msg);
            delay_ms(2000);  // Give it some time to connect
        }
        return false;
    }

    /*
     * Start client mode (Station)
     */

    bool WiFiConfig::StartSTA() {
        //stop active service
        wifi_services.end();
        //Sanity check
        if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
            WiFi.disconnect();
        }
        if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
            WiFi.softAPdisconnect();
        }

        WiFi.enableAP(false);

        // Set the number of receive and transmit buffers that the
        // WiFi stack can use.  Making these numbers too large
        // can eat up a lot of memory at 1.6K per buffer.  It
        // can be especially bad when there are many dynamic buffers,
        // If there are too few Rx buffers, file upload can fail,
        // possibly due to IP packet fragments getting lost.  The limit
        // for what works seems to be 4 static, 4 dynamic.
        // allowing external network traffic to use a lot of the heap.
        // The bawin parameters are for AMPDU aggregation.
        // rx: static dynamic bawin  tx: static dynamic bawin cache
        WiFi.setBuffers(4, 5, 0, 4, 0, 0, 4);

        //SSID
        const char* SSID = wifi_sta_ssid->get();
        if (strlen(SSID) == 0) {
            log_info("STA SSID is not set");
            return false;
        }
        //Hostname needs to be set before mode to take effect
        WiFi.setHostname(wifi_hostname->get());
        WiFi.mode(WIFI_STA);
        WiFi.setMinSecurity(static_cast<wifi_auth_mode_t>(wifi_sta_min_security->get()));
        WiFi.setScanMethod(wifi_fast_scan->get() ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
        //Get parameters for STA
        //password
        const char* password = wifi_sta_password->get();
        int8_t      IP_mode  = wifi_sta_mode->get();
        int32_t     IP       = wifi_sta_ip->get();
        int32_t     GW       = wifi_sta_gateway->get();
        int32_t     MK       = wifi_sta_netmask->get();
        //if not DHCP
        if (IP_mode != DHCP_MODE) {
            IPAddress ip(IP), mask(MK), gateway(GW);
            WiFi.config(ip, gateway, mask);
        }
        if (WiFi.begin(SSID, (strlen(password) > 0) ? password : NULL)) {
            log_info("Connecting to STA SSID:" << SSID);
            return ConnectSTA2AP();
        } else {
            log_info("Starting client failed");
            return false;
        }
    }

    /**
     * Setup and start Access point
     */

    bool WiFiConfig::StartAP() {
        //Sanity check
        if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
            WiFi.disconnect();
        }
        if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
            WiFi.softAPdisconnect();
        }

        WiFi.enableSTA(false);
        WiFi.mode(WIFI_AP);

        const char* country = wifi_ap_country->getStringValue();
        if (ESP_OK != esp_wifi_set_country_code(country, true)) {
            log_error("failed to set Wifi regulatory domain to " << country);
        }

        //auto comms = config->_comms;  // _comms is automatically created in afterParse
        //auto ap    = comms->_apConfig;
        // ap might be nullpt if there is an explicit comms: with no wifi_ap:
        // If a _comms node is created automatically, a default AP config is created too
        // if (!ap) {
        //     return false;
        // }

        //Get parameters for AP
        //SSID
        const char* SSID = wifi_ap_ssid->get();
        if (strlen(SSID) == 0) {
            SSID = DEFAULT_AP_SSID;
        }

        const char* password = wifi_ap_password->get();

        int8_t channel = int8_t(wifi_ap_channel->get());
        if (channel == 0) {
            channel = DEFAULT_AP_CHANNEL;
        }

        IPAddress ip(wifi_ap_ip->get());
        IPAddress mask;
        mask.fromString(DEFAULT_AP_MK);

        log_info("AP SSID " << SSID << " IP " << IP_string(ip) << " mask " << IP_string(mask) << " channel " << channel);

        //Set static IP
        WiFi.softAPConfig(ip, ip, mask);

        //Start AP
        if (WiFi.softAP(SSID, (strlen(password) > 0) ? password : NULL, channel)) {
            log_info("AP started");
            return true;
        }

        log_info("AP did not start");
        return false;
    }

    /**
     * Stop WiFi
     */

    void WiFiConfig::reset() {
        WiFi.persistent(false);
        WiFi.disconnect(true);
        WiFi.enableSTA(false);
        WiFi.enableAP(false);
        WiFi.mode(WIFI_OFF);
    }

    void WiFiConfig::StopWiFi() {
        if (WiFi.getMode() != WIFI_MODE_NULL) {
            if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.disconnect(true);
            }
            if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.softAPdisconnect(true);
            }
            wifi_services.end();
            WiFi.enableSTA(false);
            WiFi.enableAP(false);
            WiFi.mode(WIFI_OFF);
        }
        log_info("WiFi Off");
    }

    /**
     * begin WiFi setup
     */
    bool WiFiConfig::begin() {
        //stop active services
        wifi_services.end();

        switch (wifi_mode->get()) {
            case WiFiOff:
                log_info("WiFi is disabled");
                return false;
            case WiFiSTA:
                if (StartSTA()) {
                    goto wifi_on;
                }
                goto wifi_off;
            case WiFiFallback:
                if (StartSTA()) {
                    goto wifi_on;
                } else {  // STA failed, reset
                    WiFi.mode(WIFI_OFF);
                    esp_wifi_restore();
                    delay(100);
                }
                // fall through to fallback to AP mode
            case WiFiAP:
                if (StartAP()) {
                    goto wifi_on;
                }
                goto wifi_off;
        }

    wifi_off:
        log_info("WiFi off");
        WiFi.mode(WIFI_OFF);
        return false;

    wifi_on:
        //Get hostname
        _hostname = wifi_hostname->get();

        //setup events
        if (!_events_registered) {
            //cumulative function and no remove so only do once
            WiFi.onEvent(WiFiConfig::WiFiEvent);
            _events_registered = true;
        }
        esp_wifi_set_ps(WIFI_PS_NONE);
        log_info("WiFi on");
        wifi_services.begin();
        return true;
    }

    /**
     * End WiFi
     */
    void WiFiConfig::end() { StopWiFi(); }

    /**
     * Reset ESP
     */
    void WiFiConfig::reset_settings() {
        bool error = false;
        // XXX this is probably wrong for YAML land.
        // We might want this function to go away.
        for (Setting* s : Setting::List) {
            if (s->getDescription()) {
                s->setDefault();
            }
        }
        // TODO commit the changes and check that for errors
        if (error) {
            log_info("WiFi reset error");
        }
        log_info("WiFi reset done");
    }
    bool WiFiConfig::isOn() { return !(WiFi.getMode() == WIFI_MODE_NULL); }

    /**
     * Handle not critical actions that must be done in sync environment
     */
    void WiFiConfig::handle() { wifi_services.handle(); }

    // Used by js/scanwifidlg.js

    Error WiFiConfig::listAPs(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP410
        JSONencoder j(false, &out);
        j.begin();

        if (parameter != NULL && (strstr(parameter, "json=yes")) != NULL) {
            j.member("cmd", "410");
            j.member("status", "ok");
            j.begin_array("data");
        } else {
            j.begin_array("AP_LIST");
        }

        j.begin_array("AP_LIST");
        // An initial async scanNetworks was issued at startup, so there
        // is a good chance that scan information is already available.
        int n = WiFi.scanComplete();
        switch (n) {
            case -2:                      // Scan not triggered
                WiFi.scanNetworks(true);  // Begin async scan
                break;
            case -1:  // Scan in progress
                break;
            default:
                for (int i = 0; i < n; ++i) {
                    j.begin_object();
                    j.member("SSID", WiFi.SSID(i).c_str());
                    j.member("SIGNAL", wifi_config.getSignal(WiFi.RSSI(i)));
                    j.member("IS_PROTECTED", WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                    //            j->member("IS_PROTECTED", WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1");
                    j.end_object();
                }
                WiFi.scanDelete();
                // Restart the scan in async mode so new data will be available
                // when we ask again.
                n = WiFi.scanComplete();
                if (n == -2) {
                    WiFi.scanNetworks(true);
                }
                break;
        }
        j.end_array();
        j.end();
        return Error::Ok;
    }

    WiFiConfig::~WiFiConfig() { end(); }
}
#endif
