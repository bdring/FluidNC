// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"
#include "Machine/MachineConfig.h"
#include <sstream>
#include <iomanip>

#include "Channel.h"         // Channel
#include "Error.h"           // Error
#include "Module.h"          // Module
#include "Authentication.h"  // AuthenticationLevel

#include "Main.h"

#include "WebUIServer.h"           // Web_Server::port()
#include "TelnetServer.h"          // TelnetServer::port()
#include "NotificationsService.h"  // notificationsservice

#include <WiFi.h>
#include <esp_wifi.h>
#include "Driver/localfs.h"
#include <string>
#include <cstring>

#include <esp_ota_ops.h>

// For modern compilers, we need some different function calls. Rather than
// attempting to rewrite everything, let's just define the problem away:

#include <esp_idf_version.h>

#if ESP_IDF_VERSION_MAJOR >= 5
#    include <esp_wifi.h>

#    define tcpip_adapter_dhcp_status_t esp_netif_dhcp_status_t
#    define tcpip_adapter_dhcpc_get_status esp_netif_dhcpc_get_status
#    define tcpip_adapter_get_ip_info esp_netif_get_ip_info

#    define tcpip_adapter_dhcps_get_status esp_netif_dhcps_get_status
#    define tcpip_adapter_ip_info_t esp_netif_ip_info_t
#    define tcpip_adapter_sta_list_t wifi_sta_list_t
#    define tcpip_adapter_get_sta_list(station, list) esp_wifi_ap_get_sta_list(list)

#    define SYSTEM_EVENT_WIFI_READY WIFI_EVENT_WIFI_READY
#    define SYSTEM_EVENT_SCAN_DONE WIFI_EVENT_SCAN_DONE
#    define SYSTEM_EVENT_STA_START WIFI_EVENT_STA_START
#    define SYSTEM_EVENT_STA_STOP WIFI_EVENT_STA_STOP
#    define SYSTEM_EVENT_STA_CONNECTED WIFI_EVENT_STA_CONNECTED
#    define SYSTEM_EVENT_STA_DISCONNECTED WIFI_EVENT_STA_DISCONNECTED
#    define SYSTEM_EVENT_STA_AUTHMODE_CHANGE WIFI_EVENT_STA_AUTHMODE_CHANGE
#    define SYSTEM_EVENT_STA_GOT_IP IP_EVENT_STA_GOT_IP
#    define SYSTEM_EVENT_STA_LOST_IP IP_EVENT_STA_LOST_IP
#    define SYSTEM_EVENT_STA_WPS_ER_SUCCESS WIFI_EVENT_STA_WPS_ER_SUCCESS
#    define SYSTEM_EVENT_STA_WPS_ER_FAILED WIFI_EVENT_STA_WPS_ER_FAILED
#    define SYSTEM_EVENT_STA_WPS_ER_TIMEOUT WIFI_EVENT_STA_WPS_ER_TIMEOUT
#    define SYSTEM_EVENT_STA_WPS_ER_PIN WIFI_EVENT_STA_WPS_ER_PIN
#    define SYSTEM_EVENT_AP_START WIFI_EVENT_AP_START
#    define SYSTEM_EVENT_AP_STOP WIFI_EVENT_AP_STOP
#    define SYSTEM_EVENT_AP_STACONNECTED WIFI_EVENT_AP_STACONNECTED
#    define SYSTEM_EVENT_AP_STADISCONNECTED WIFI_EVENT_AP_STADISCONNECTED
#    define SYSTEM_EVENT_AP_PROBEREQRECVED WIFI_EVENT_AP_PROBEREQRECVED
#    define SYSTEM_EVENT_ETH_GOT_IP IP_EVENT_ETH_GOT_IP

#    define TCPIP_ADAPTER_DHCP_STARTED ESP_NETIF_DHCP_STARTED
#    define TCPIP_ADAPTER_DHCP_STOPPED ESP_NETIF_DHCP_STOPPED

// This doesn't make any sense.
#    define GetIPAddr(x) "0.0.0.0"

esp_netif_t* TCPIP_ADAPTER_IF_AP  = nullptr;
esp_netif_t* TCPIP_ADAPTER_IF_STA = nullptr;
#else
#    define GetIPAddr(x) IP_string(IPAddress(x.ip.addr))
#endif

namespace WebUI {
    enum WiFiStartupMode {
        WiFiOff = 0,
        WiFiSTA,
        WiFiAP,
        WiFiFallback,  // Try STA and fall back to AP if STA fails
    };

    const enum_opt_t wifiModeOptions = {
        { "Off", WiFiOff },
        { "STA", WiFiSTA },
        { "AP", WiFiAP },
        { "STA>AP", WiFiFallback },
    };

    const enum_opt_t wifiPsModeOptions = {
        { "None", WIFI_PS_NONE },
        { "Min", WIFI_PS_MIN_MODEM },
        { "Max", WIFI_PS_MAX_MODEM },
    };

    enum WiFiCountry {
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

    const enum_opt_t wifiCountryOptions = {
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

    static const char* NULL_IP = "0.0.0.0";

    //boundaries
    static constexpr int MAX_SSID_LENGTH     = 32;
    static constexpr int MIN_SSID_LENGTH     = 0;  // Allow null SSIDs as a way to disable
    static constexpr int MAX_PASSWORD_LENGTH = 64;
    //min size of password is 0 or upper than 8 char
    //so let set min is 8
    static constexpr int MIN_PASSWORD_LENGTH = 8;
    static constexpr int MAX_HOSTNAME_LENGTH = 32;
    static constexpr int MIN_HOSTNAME_LENGTH = 1;

    static constexpr int DHCP_MODE   = 0;
    static constexpr int STATIC_MODE = 1;

    static const enum_opt_t staModeOptions = {
        { "DHCP", DHCP_MODE },
        { "Static", STATIC_MODE },
    };

    static const enum_opt_t staSecurityOptions = {
        { "OPEN", WIFI_AUTH_OPEN },
        { "WEP", WIFI_AUTH_WEP },
        { "WPA-PSK", WIFI_AUTH_WPA_PSK },
        { "WPA2-PSK", WIFI_AUTH_WPA2_PSK },
        { "WPA-WPA2-PSK", WIFI_AUTH_WPA_WPA2_PSK },
        { "WPA2-ENTERPRISE", WIFI_AUTH_WPA2_ENTERPRISE },
        { "WPA3-PSK", WIFI_AUTH_WPA3_PSK },
        { "WPA2-WPA3-PSK", WIFI_AUTH_WPA2_WPA3_PSK },
        { "WAPI-PSK", WIFI_AUTH_WAPI_PSK },
        { "WPA3-ENT-192", WIFI_AUTH_WPA3_ENT_192 },
    };

    class PasswordSetting : public StringSetting {
    public:
        PasswordSetting(const char* description, const char* grblName, const char* name, const char* defVal) :
            StringSetting(description, WEBSET, WA, grblName, name, defVal, MIN_PASSWORD_LENGTH, MAX_PASSWORD_LENGTH) {
            load();
        }
        const char* getDefaultString() { return "********"; }
        const char* getStringValue() { return "********"; }
    };

    class HostnameSetting : public StringSetting {
    public:
        HostnameSetting(const char* description, const char* grblName, const char* name, const char* defVal) :
            StringSetting(description, WEBSET, WA, grblName, name, defVal, MIN_HOSTNAME_LENGTH, MAX_HOSTNAME_LENGTH) {
            load();
        }
        Error setStringValue(std::string_view s) {
            // Hostname strings may contain only letters, digits and -
            for (auto const& c : s) {
                if (c == ' ' || !(isdigit(c) || isalpha(c) || c == '-')) {
                    return Error::InvalidValue;
                }
            }
            return StringSetting::setStringValue(s);
        }
    };

    static EnumSetting*     _mode;
    static StringSetting*   _sta_ssid;
    static HostnameSetting* _hostname;
    static IntSetting*      _ap_channel;
    static IPaddrSetting*   _ap_ip;
    static PasswordSetting* _ap_password;
    static StringSetting*   _ap_ssid;
    static EnumSetting*     _ap_country;
    static IPaddrSetting*   _sta_netmask;
    static IPaddrSetting*   _sta_gateway;
    static IPaddrSetting*   _sta_ip;
    static EnumSetting*     _sta_mode;
    static EnumSetting*     _fast_scan;
    static EnumSetting*     _sta_min_security;
    static PasswordSetting* _sta_password;
    static EnumSetting*     _wifi_ps_mode;

    class WiFiConfig : public Module {
    private:
        static void print_mac(Channel& out, const char* prefix, const char* mac) { log_stream(out, prefix << " (" << mac << ")"); }

        static Error showIP(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
            log_stream(out, parameter << IP_string(WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP()));
            return Error::Ok;
        }

        static Error showSetStaParams(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP103
            if (*parameter == '\0') {
                log_stream(out,
                           "IP:" << _sta_ip->getStringValue() << " GW:" << _sta_gateway->getStringValue()
                                 << " MSK:" << _sta_netmask->getStringValue());
                return Error::Ok;
            }
            std::string gateway, netmask, ip;
            if (!(get_param(parameter, "GW", gateway) && get_param(parameter, "MSK", netmask) && get_param(parameter, "IP", ip))) {
                return Error::InvalidValue;
            }

            Error err = _sta_ip->setStringValue(ip);
            if (err == Error::Ok) {
                err = _sta_netmask->setStringValue(netmask);
            }
            if (err == Error::Ok) {
                err = _sta_gateway->setStringValue(gateway);
            }
            return err;
        }

        void wifi_stats(JSONencoder& j) {
            j.id_value_object("Sleep mode", WiFi.getSleep() ? "Modem" : "None");
            auto mode = WiFi.getMode();
            if (mode != WIFI_OFF) {
                //Is OTA available ?
                size_t flashsize = 0;
                if (esp_ota_get_running_partition()) {
                    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                    if (partition) {
                        flashsize = partition->size;
                    }
                }
                j.id_value_object("Available Size for update", formatBytes(flashsize));
                j.id_value_object("Available Size for LocalFS", formatBytes(localfs_size()));
                j.id_value_object("Web port", WebUI_Server::port());
                j.id_value_object("Data port", TelnetServer::port());
                j.id_value_object("Hostname", WiFi.getHostname());
            }

            switch (mode) {
                case WIFI_STA:

                    j.id_value_object("Current WiFi Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");

                    if (WiFi.isConnected()) {  //in theory no need but ...
                        j.id_value_object("Connected to", WiFi.SSID().c_str());
                        j.id_value_object("Signal", std::string("") + std::to_string(getSignal(WiFi.RSSI())) + "%");

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

                        j.id_value_object("Phy Mode", modeName);
                        j.id_value_object("Channel", WiFi.channel());

                        j.id_value_object("IP Mode", _sta_mode->getStringValue());
                        j.id_value_object("IP", IP_string(WiFi.localIP()));
                        j.id_value_object("Gateway", IP_string(WiFi.gatewayIP()));
                        j.id_value_object("Mask", IP_string(WiFi.subnetMask()));
                        j.id_value_object("DNS", IP_string(WiFi.dnsIP()));

                    }  //this is web command so connection => no command
                    j.id_value_object("Disabled Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    break;
                case WIFI_AP:
                    j.id_value_object("Current WiFi Mode", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    wifi_config_t  conf;
                    wifi_country_t country;
                    esp_wifi_get_config(WIFI_IF_AP, &conf);
                    esp_wifi_get_country(&country);
                    j.id_value_object("SSID", (const char*)conf.ap.ssid);
                    j.id_value_object("Visible", (conf.ap.ssid_hidden == 0 ? "Yes" : "No"));
                    j.id_value_object("Radio country set",
                                      std::string("") + country.cc[0] + country.cc[1] + " (channels " + std::to_string(country.schan) +
                                          "-" + std::to_string((country.schan + country.nchan - 1)) + ", max power " +
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

                    j.id_value_object("Authentication", mode);
                    j.id_value_object("Max Connections", conf.ap.max_connection);
                    j.id_value_object("IP", IP_string(WiFi.softAPIP()));

                    // Retrieving the configured gateway and netmask from the Arduino WiFi class
                    // is very tricky, so we just regurgitate the values that we passed in when
                    // starting the AP
                    j.id_value_object("Gateway", IP_string(WiFi.softAPIP()));
                    j.id_value_object("Mask", "255.255.255.0");

#ifdef IDFBUILD
                    wifi_sta_list_t          station;
                    tcpip_adapter_sta_list_t tcpip_sta_list;
                    esp_wifi_ap_get_sta_list(&station);
                    tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                    j.id_value_object("Connected channels", station.num);

                    for (int i = 0; i < station.num; i++) {
                        j.id_value_object("", std::string("") + mac2str(tcpip_sta_list.sta[i].mac) + " " + GetIPAddr(tcpip_sta_list.sta[i]));
                    }
#else
                    wifi_sta_list_t      station;
                    esp_netif_sta_list_t netif_sta_list;
                    esp_wifi_ap_get_sta_list(&station);
                    esp_netif_get_sta_list(&station, &netif_sta_list);

                    j.id_value_object("Connected channels", station.num);

                    for (size_t i = 0; i < station.num; i++) {
                        j.id_value_object("",
                                          std::string("") + mac2str(netif_sta_list.sta[i].mac) + " " +
                                              IP_string(IPAddress(netif_sta_list.sta[i].ip.addr)));
                    }
#endif

                    j.id_value_object("Disabled Mode", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                    break;
                case WIFI_AP_STA:  //we should not be in this state but just in case ....
                    j.id_value_object("Mixed", std::string("STA (") + WiFi.macAddress().c_str() + ")");
                    j.id_value_object("Mixed", std::string("AP (") + WiFi.softAPmacAddress().c_str() + ")");
                    break;
                default:  //we should not be there if no wifi ....

                    j.id_value_object("Current WiFi Mode", "Off");
                    break;
            }
        }

        void status_report(Channel& out) {
            log_stream(out, "Sleep mode: " << (WiFi.getSleep() ? "Modem" : "None"));
            auto mode = WiFi.getMode();
            if (mode != WIFI_OFF) {
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
                log_stream(out, "Web port: " << WebUI_Server::port());
                log_stream(out, "Hostname: " << WiFi.getHostname());
            }

            switch (mode) {
                case WIFI_STA:
                    print_mac(out, "Current WiFi Mode: STA", WiFi.macAddress().c_str());

                    if (WiFi.isConnected()) {  //in theory no need but ...
                        log_stream(out, "Connected to: " << WiFi.SSID().c_str());
                        log_stream(out, "Signal: " << getSignal(WiFi.RSSI()) << "%");

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

                        log_stream(out, "IP Mode: " << _sta_mode->getStringValue());
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
                                                     << (country.schan + country.nchan - 1) << ", max power " << country.max_tx_power
                                                     << "dBm)");

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

                    log_stream(out, "IP: " << IP_string(WiFi.softAPIP()));

                    // Retrieving the configured gateway and netmask from the Arduino WiFi class
                    // is very tricky, so we just regurgitate the values that we passed in when
                    // starting the AP
                    log_stream(out, "Gateway: " << IP_string(IPAddress(WiFi.softAPIP())));
                    log_stream(out, "Mask: 255.255.255.0");

#ifdef IDFBUILD
                    wifi_sta_list_t          station;
                    tcpip_adapter_sta_list_t tcpip_sta_list;
                    esp_wifi_ap_get_sta_list(&station);
                    tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                    log_stream(out, "Connected channels: " << station.num);

                    for (int i = 0; i < station.num; i++) {
                        log_stream(out, mac2str(tcpip_sta_list.sta[i].mac) << " " << GetIPAddr(tcpip_sta_list.sta[i]));
                    }
#else
                    wifi_sta_list_t      station;
                    esp_netif_sta_list_t netif_sta_list;
                    esp_wifi_ap_get_sta_list(&station);
                    esp_netif_get_sta_list(&station, &netif_sta_list);
                    log_stream(out, "Connected channels: " << station.num);

                    for (size_t i = 0; i < station.num; i++) {
                        log_stream(out, mac2str(netif_sta_list.sta[i].mac) << " " << IP_string(IPAddress(netif_sta_list.sta[i].ip.addr)));
                    }
#endif

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
            s << (NotificationsService::started() ? "Enabled" : "Disabled");
            if (NotificationsService::started()) {
                s << "(" << NotificationsService::getTypeString() << ")";
            }
        }

        static const char* modeName() {
            switch (WiFi.getMode()) {
                case WIFI_OFF:
                    return "None";
                case WIFI_STA:
                    return "STA";
                case WIFI_AP:
                    return "AP";
                default:
                    return "?";
            }
        }

        bool _events_registered = false;

        static Error showFwInfoJSON(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            if (strstr(parameter, "json=yes") != NULL) {
                JSONencoder j(&out);
                j.begin();
                j.member("cmd", "800");
                j.member("status", "ok");
                j.begin_member_object("data");
                j.member("FWVersion", git_info);
                j.member("FWTarget", "FluidNC");
                j.member("FWTargetId", "60");
                j.member("WebUpdate", "Enabled");

                j.member("Setup", "Disabled");
                j.member("SDConnection", "direct");
                j.member("SerialProtocol", "Socket");
#ifdef ENABLE_AUTHENTICATION
                j.member("Authentication", "Enabled");
#else
                j.member("Authentication", "Disabled");
#endif
                j.member("WebCommunication", "Synchronous");

                switch (WiFi.getMode()) {
                    case WIFI_AP:
                        j.member("WebSocketIP", IP_string(WiFi.softAPIP()));
                        break;
                    case WIFI_STA:
                        j.member("WebSocketIP", IP_string(WiFi.localIP()));
                        break;
                    case WIFI_AP_STA:
                        j.member("WebSocketIP", IP_string(WiFi.softAPIP()));
                        break;
                    default:
                        j.member("WebSocketIP", "0.0.0.0");
                        break;
                }

                j.member("WebSocketPort", std::to_string(WebUI_Server::port()));
                j.member("HostName", WiFi.getHostname());
                j.member("WiFiMode", modeName());
                j.member("FlashFileSystem", "LittleFS");
                j.member("HostPath", "/");
                j.member("Time", "none");
                std::string axisLetters;
                for (axis_t axis; axis < Axes::_numberAxis; axis++) {
                    axisLetters += Axes::axisName(axis);
                }
                j.member("Axisletters", axisLetters);
                j.end_object();
                j.end();
                return Error::Ok;
            }

            return Error::InvalidStatement;
        }

        static Error showFwInfo(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP800
            if (parameter != NULL && paramIsJSON(parameter)) {
                return showFwInfoJSON(parameter, auth_level, out);
            }

            LogStream s(out, "FW version: FluidNC ");
            s << git_info;
            // TODO: change grbl-embedded to FluidNC after fixing WebUI
            s << " # FW target:grbl-embedded  # FW HW:";

            // std::error_code ec;
            // FluidPath { "/sd", ec };
            // s << (ec ? "No SD" : "Direct SD");

            // We do not check the SD presence here because if the SD card is out,
            // WebUI will switch to M20 for SD access, which is wrong for FluidNC
            s << "Direct SD";

            s << "  # primary sd:";

            (config->_sdCard->config_ok) ? s << "/sd" : s << "none";

            s << " # secondary sd:none ";

            s << " # authentication:";
#ifdef ENABLE_AUTHENTICATION
            s << "yes";
#else
            s << "no";
#endif
            s << " # webcommunication: Sync: ";
            s << std::to_string(WebUI_Server::port());
#if 0
            // If we omit the explicit IP address for the websocket,
            // WebUI will use the same IP address that it uses for
            // HTTP, with the port number as above.  That is better
            // than providing an explicit address, because if the WiFi
            // drops and comes back up again, DHCP might assign a
            // different IP address so the one provided below would no
            // longer work.  But if we are using an MDNS address like
            // fluidnc.local, a websocket reconnection will succeed
            // because MDNS will offer the new IP address.
            s << ":";
            switch (WiFi.getMode()) {
                case WIFI_AP:
                    s << IP_string(WiFi.softAPIP());
                    break;
                case WIFI_STA:
                    s << IP_string(WiFi.localIP());
                    break;
                case WIFI_AP_STA:
                    s << IP_string(WiFi.softAPIP());
                    break;
                default:
                    s << "0.0.0.0";
                    break;
            }
#endif
            s << " # hostname:";
            s << WiFi.getHostname();
            if (WiFi.getMode() == WIFI_AP) {
                s << "(AP mode)";
            }

            //to save time in decoding `?`
            s << " # axis:" << Axes::_numberAxis;
            return Error::Ok;
        }

        /**
     * WiFi events
     * WIFI_EVENT_WIFI_READY               < WiFi ready
     * WIFI_EVENT_SCAN_DONE                < finish scanning AP
     * WIFI_EVENT_STA_START                < station start
     * WIFI_EVENT_STA_STOP                 < station stop
     * WIFI_EVENT_STA_CONNECTED            < station connected to AP
     * WIFI_EVENT_STA_DISCONNECTED         < station disconnected from AP
     * WIFI_EVENT_STA_AUTHMODE_CHANGE      < the auth mode of AP connected by station changed
     * IP_EVENT_STA_GOT_IP                 < station got IP from connected AP
     * IP_EVENT_STA_LOST_IP                < station lost IP and the IP is reset to 0
     * WIFI_EVENT_STA_WPS_ER_SUCCESS        < station wps succeeds in enrollee mode
     * WIFI_EVENT_STA_WPS_ER_FAILED         < station wps fails in enrollee mode
     * WIFI_EVENT_STA_WPS_ER_TIMEOUT        < station wps timeout in enrollee mode
     * WIFI_EVENT_STA_WPS_ER_PIN            < station wps pin code in enrollee mode
     * WIFI_EVENT_AP_START                  < soft-AP start
     * WIFI_EVENT_AP_STOP                   < soft-AP stop
     * WIFI_EVENT_AP_STACONNECTED           < a station connected to soft-AP
     * WIFI_EVENT_AP_STADISCONNECTED        < a station disconnected from soft-AP
     * WIFI_EVENT_AP_PROBEREQRECVED         < Receive probe request packet in soft-AP interface
     * SYSTEM_EVENT_GOT_IP6                 < station or ap or ethernet interface v6IP addr is preferred
     * SYSTEM_EVENT_ETH_START               < ethernet start
     * SYSTEM_EVENT_ETH_STOP                < ethernet stop
     * SYSTEM_EVENT_ETH_CONNECTED           < ethernet phy link up
     * SYSTEM_EVENT_ETH_DISCONNECTED        < ethernet phy link down
     * IP_EVENT_ETH_GOT_IP                  < ethernet got IP from connected AP
     * SYSTEM_EVENT_MAX
     */

        static void WiFiEvent(WiFiEvent_t event) {
            static bool disconnect_seen = false;
            switch (event) {
                case SYSTEM_EVENT_STA_GOT_IP:
                    log_info_to(Console, "Got IP: " << IP_string(WiFi.localIP()));
                    break;
                case WIFI_EVENT_STA_DISCONNECTED:
                    if (!disconnect_seen) {
                        log_info_to(Console, "WiFi Disconnected");
                        disconnect_seen = true;
                    }
                    break;
                case WIFI_EVENT_STA_START:
                    break;
                case WIFI_EVENT_STA_STOP:
                    break;
                case WIFI_EVENT_STA_CONNECTED:
                    disconnect_seen = false;
                    log_info_to(Console, "WiFi STA Connected");
                    break;
                default:
                    log_debug_to(Console, "WiFi event: " << (int)event);
                    break;
            }
        }

        static int32_t getSignal(int32_t RSSI) {
            if (RSSI <= -100) {
                return 0;
            }
            if (RSSI >= -50) {
                return 100;
            }
            return 2 * (RSSI + 100);
        }

        static bool ConnectSTA2AP() {
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

        static bool StartSTA() {
            //Sanity check
            auto mode = WiFi.getMode();
            if (mode == WIFI_STA || mode == WIFI_AP_STA) {
                WiFi.disconnect();
            }

            if (mode == WIFI_AP || mode == WIFI_AP_STA) {
                WiFi.softAPdisconnect();
            }

            WiFi.enableAP(false);

            //SSID
            const char* SSID = _sta_ssid->get();
            if (strlen(SSID) == 0) {
                log_info("STA SSID is not set");
                return false;
            }
            //Hostname needs to be set before mode to take effect
            WiFi.setHostname(_hostname->get());
            WiFi.mode(WIFI_STA);
            WiFi.setMinSecurity(static_cast<wifi_auth_mode_t>(_sta_min_security->get()));
            WiFi.setScanMethod(_fast_scan->get() ? WIFI_FAST_SCAN : WIFI_ALL_CHANNEL_SCAN);
            WiFi.setAutoReconnect(true);
            //Get parameters for STA
            //password
            const char* password = _sta_password->get();
            int8_t      IP_mode  = _sta_mode->get();
            int32_t     IP       = _sta_ip->get();
            int32_t     GW       = _sta_gateway->get();
            int32_t     MK       = _sta_netmask->get();
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

        static bool StartAP() {
            //Sanity check
            if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.disconnect();
            }
            if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
                WiFi.softAPdisconnect();
            }

            WiFi.enableSTA(false);
            WiFi.mode(WIFI_AP);

            const char* country = _ap_country->getStringValue();
            if (ESP_OK != esp_wifi_set_country_code(country, true)) {
                log_error("failed to set Wifi regulatory domain to " << country);
            }

            //Get parameters for AP
            const char* SSID = _ap_ssid->get();

            const char* password = _ap_password->get();

            int8_t channel = int8_t(_ap_channel->get());

            IPAddress ip(_ap_ip->get());
            IPAddress mask;
            mask.fromString("255.255.255.0");

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

        static void reset() {
            WiFi.persistent(false);
            WiFi.disconnect(true);
            WiFi.enableSTA(false);
            WiFi.enableAP(false);
            WiFi.mode(WIFI_OFF);
        }

        static void StopWiFi() {
            if (WiFi.getMode() != WIFI_OFF) {
                if ((WiFi.getMode() == WIFI_STA) || (WiFi.getMode() == WIFI_AP_STA)) {
                    WiFi.disconnect(true);
                }
                if ((WiFi.getMode() == WIFI_AP) || (WiFi.getMode() == WIFI_AP_STA)) {
                    WiFi.softAPdisconnect(true);
                }
                // wifi_services.end();
                WiFi.enableSTA(false);
                WiFi.enableAP(false);
                WiFi.mode(WIFI_OFF);
            }
            log_info("WiFi Off");
        }

        static const char* mac2str(uint8_t mac[8]) {
            static char macstr[18];
            if (0 > sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5])) {
                strcpy(macstr, "00:00:00:00:00:00");
            }
            return macstr;
        }

        static std::string station_info() {
            std::string result;

            auto mode = WiFi.getMode();
            if (mode == WIFI_STA || mode == WIFI_AP_STA) {
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

        static std::string ap_info() {
            std::string result;

            auto mode = WiFi.getMode();
            if (mode == WIFI_AP || mode == WIFI_AP_STA) {
                if (WiFi.getMode() == WIFI_AP_STA) {
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

        static bool isOn() {
            return !(WiFi.getMode() == WIFI_OFF);
        }

        // Used by js/scanwifidlg.js

        static Error listAPs(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP410
            JSONencoder j(&out);
            j.begin();

            if (parameter != NULL && (strstr(parameter, "json=yes")) != NULL) {
                j.member("cmd", "410");
                j.member("status", "ok");
                j.begin_array("data");
            } else {
                j.begin_array("AP_LIST");
            }

            // An initial async scanNetworks was issued at startup, so there
            // is a good chance that scan information is already available.
            int32_t n;
            while (true) {
                n = WiFi.scanComplete();
                if (n >= 0) {  // Scan completed with n results
                    break;
                }
                if (n == WIFI_SCAN_FAILED) {  // Begin async scan
                    //                async hidden passive ms_per_chan
                    WiFi.scanNetworks(true, false, false, 1000);
                }
                // Else WIFI_SCAN_RUNNING
                delay(1000);
            }

            for (int i = 0; i < n; ++i) {
                j.begin_object();
                j.member("SSID", WiFi.SSID(i).c_str());
                j.member("SIGNAL", getSignal(WiFi.RSSI(i)));
                j.member("IS_PROTECTED", WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                //            j->member("IS_PROTECTED", WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1");
                j.end_object();
            }
            WiFi.scanDelete();
            // Restart the scan in async mode so new data will be available
            // when we ask again.
            WiFi.scanNetworks(true);
            j.end_array();
            j.end();
            return Error::Ok;
        }

    public:
        WiFiConfig(const char* name) : Module(name) {}

        void init() {
            _sta_ssid    = new StringSetting("Station SSID", WEBSET, WA, "ESP100", "Sta/SSID", "", MIN_SSID_LENGTH, MAX_SSID_LENGTH);
            _hostname    = new HostnameSetting("Hostname", "ESP112", "Hostname", "fluidnc");
            _ap_channel  = new IntSetting("AP Channel", WEBSET, WA, "ESP108", "AP/Channel", 1, 1, 14);
            _ap_ip       = new IPaddrSetting("AP Static IP", WEBSET, WA, "ESP107", "AP/IP", "192.168.0.1");
            _ap_password = new PasswordSetting("AP Password", "ESP106", "AP/Password", "12345678");
            _ap_ssid     = new StringSetting("AP SSID", WEBSET, WA, "ESP105", "AP/SSID", "FluidNC", MIN_SSID_LENGTH, MAX_SSID_LENGTH);
            _ap_country  = new EnumSetting("AP regulatory domain", WEBSET, WA, NULL, "AP/Country", WiFiCountry01, &wifiCountryOptions);
            _sta_netmask = new IPaddrSetting("Station Static Mask", WEBSET, WA, NULL, "Sta/Netmask", NULL_IP);
            _sta_gateway = new IPaddrSetting("Station Static Gateway", WEBSET, WA, NULL, "Sta/Gateway", NULL_IP);
            _sta_ip      = new IPaddrSetting("Station Static IP", WEBSET, WA, NULL, "Sta/IP", NULL_IP);
            _sta_mode    = new EnumSetting("Station IP Mode", WEBSET, WA, "ESP102", "Sta/IPMode", DHCP_MODE, &staModeOptions);
            _fast_scan   = new EnumSetting("WiFi Fast Scan", WEBSET, WA, NULL, "WiFi/FastScan", 0, &onoffOptions);
            _sta_min_security =
                new EnumSetting("Station IP Mode", WEBSET, WA, NULL, "Sta/MinSecurity", WIFI_AUTH_WPA2_PSK, &staSecurityOptions);
            _sta_password = new PasswordSetting("Station Password", "ESP101", "Sta/Password", "");

            _mode         = new EnumSetting("WiFi mode", WEBSET, WA, "ESP116", "WiFi/Mode", WiFiFallback, &wifiModeOptions);
            _wifi_ps_mode = new EnumSetting("WiFi power saving mode", WEBSET, WA, NULL, "WiFi/PsMode", WIFI_PS_NONE, &wifiPsModeOptions);

            new WebCommand(NULL, WEBCMD, WU, "ESP410", "WiFi/ListAPs", listAPs);
            new WebCommand(NULL, WEBCMD, WG, "ESP800", "Firmware/Info", showFwInfo, anyState);

            new WebCommand(NULL, WEBCMD, WG, "ESP111", "System/IP", showIP);
            new WebCommand("IP=ipaddress MSK=netmask GW=gateway", WEBCMD, WA, "ESP103", "Sta/Setup", showSetStaParams);

            //stop active services
            // wifi_services.end();

            switch (_mode->get()) {
                case WiFiOff:
                    log_info("WiFi is disabled");
                    return;
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
                        delay_ms(100);
                    }
                    // fall through to fallback to AP mode
                    [[fallthrough]];
                case WiFiAP:
                    if (StartAP()) {
                        goto wifi_on;
                    }
                    goto wifi_off;
            }

        wifi_off:
            log_info("WiFi off");
            WiFi.mode(WIFI_OFF);
            return;

        wifi_on:
            //setup events
            if (!_events_registered) {
                //cumulative function and no remove so only do once
                WiFi.onEvent(WiFiEvent);
                _events_registered = true;
            }
            esp_wifi_set_ps(WIFI_PS_NONE);
            esp_wifi_set_ps(static_cast<wifi_ps_type_t>(_wifi_ps_mode->get()));
            log_info("WiFi on");
            //        wifi_services.begin();
        }

        void deinit() override {
            StopWiFi();
        }

        void build_info(Channel& channel) {
            std::string sti = station_info();
            if (sti.length()) {
                log_msg_to(channel, sti);
            }
            std::string api = ap_info();
            if (api.length()) {
                log_msg_to(channel, api);
            }
            if (!sti.length() && !api.length()) {
                log_msg_to(channel, "No Wifi");
            }
        }

        void poll() {
            //to avoid mixed mode due to scan network
            if (WiFi.getMode() == WIFI_AP_STA) {
                // In principle it should be sufficient to check for != WIFI_SCAN_RUNNING,
                // but that does not work well.  Doing so makes scans in AP mode unreliable.
                // Sometimes the first try works, but subsequent scans fail.
                if (WiFi.scanComplete() >= 0) {
                    WiFi.enableSTA(false);
                }
            }
        }

        bool is_radio() override {
            return true;
        }

        ~WiFiConfig() {
            deinit();
        }
    };

    ModuleFactory::InstanceBuilder<WiFiConfig> __attribute__((init_priority(105))) wifi_module("wifi", true);
}
