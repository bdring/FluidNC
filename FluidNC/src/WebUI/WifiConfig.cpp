// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WifiConfig.h"

#include "../Settings.h"
#include "../Machine/MachineConfig.h"

WebUI::WiFiConfig wifi_config;

#ifdef ENABLE_WIFI
#    include "../Main.h"       // display()
#    include "Commands.h"      // COMMANDS
#    include "WifiServices.h"  // wifi_services.start() etc.
#    include "WebSettings.h"   // split_params(), get_params()

#    include "WebServer.h"             // web_server.port()
#    include "TelnetServer.h"          // telnet_server
#    include "NotificationsService.h"  // notificationsservice

#    include <WiFi.h>
#    include <esp_wifi.h>
#    include "../LocalFS.h"
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
    EnumSetting* wifi_mode;

    StringSetting* wifi_sta_ssid;
    StringSetting* wifi_sta_password;

    EnumSetting*   wifi_sta_mode;
    IPaddrSetting* wifi_sta_ip;
    IPaddrSetting* wifi_sta_gateway;
    IPaddrSetting* wifi_sta_netmask;

    StringSetting* wifi_ap_ssid;
    StringSetting* wifi_ap_password;

    IPaddrSetting* wifi_ap_ip;

    IntSetting* wifi_ap_channel;

    StringSetting* wifi_hostname;

    enum_opt_t staModeOptions = {
        { "DHCP", DHCP_MODE },
        { "Static", STATIC_MODE },
    };

    static void print_mac(const char* s, String mac, Channel& out) { out << s << " (" << mac << ")\n"; }

    static Error showIP(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP111
        out << parameter << (WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP()).toString() << '\n';
        return Error::Ok;
    }

    static Error showSetStaParams(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP103
        if (*parameter == '\0') {
            out << "IP:" << wifi_sta_ip->getStringValue();
            out << " GW:" << wifi_sta_gateway->getStringValue();
            out << " MSK:" << wifi_sta_netmask->getStringValue() << '\n';
            return Error::Ok;
        }
        if (!split_params(parameter)) {
            return Error::InvalidValue;
        }
        char* gateway = get_param("GW", false);
        char* netmask = get_param("MSK", false);
        char* ip      = get_param("IP", false);

        Error err = wifi_sta_ip->setStringValue(ip);
        if (err == Error::Ok) {
            err = wifi_sta_netmask->setStringValue(netmask);
        }
        if (err == Error::Ok) {
            err = wifi_sta_gateway->setStringValue(gateway);
        }
        return err;
    }

    void WiFiConfig::showWifiStats(Channel& out) {
        out << "Sleep mode: " << (WiFi.getSleep() ? "Modem" : "None") << '\n';
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
            out << "Available Size for update: " << formatBytes(flashsize) << '\n';
            out << "Available Size for LocalFS: " << formatBytes(LocalFS.totalBytes()) << '\n';

            out << "Web port: " << String(web_server.port()) << '\n';
            out << "Data port: " << String(telnet_server.port()) << '\n';
            out << "Hostname: " << wifi_config.Hostname() << '\n';
        }

        out << "Current WiFi Mode: ";
        switch (mode) {
            case WIFI_STA:
                print_mac("STA", WiFi.macAddress(), out);

                out << "Connected to: ";
                if (WiFi.isConnected()) {  //in theory no need but ...
                    out << WiFi.SSID() << '\n';
                    out << "Signal: " << wifi_config.getSignal(WiFi.RSSI()) << "%\n";

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
                    out << "Phy Mode: " << modeName << '\n';

                    out << "Channel: " << String(WiFi.channel()) << '\n';

                    tcpip_adapter_dhcp_status_t dhcp_status;
                    tcpip_adapter_dhcpc_get_status(TCPIP_ADAPTER_IF_STA, &dhcp_status);
                    out << "IP Mode: " << (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "DHCP" : "Static") << '\n';
                    out << "IP: " << WiFi.localIP().toString() << '\n';
                    out << "Gateway: " << WiFi.gatewayIP().toString() << '\n';
                    out << "Mask: " << WiFi.subnetMask().toString() << '\n';
                    out << "DNS: " << WiFi.dnsIP().toString() << '\n';

                }  //this is web command so connection => no command
                out << "Disabled Mode: ";
                print_mac("AP", WiFi.softAPmacAddress(), out);
                break;
            case WIFI_AP:
                print_mac("AP", WiFi.softAPmacAddress(), out);

                wifi_config_t conf;
                esp_wifi_get_config(WIFI_IF_AP, &conf);
                out << "SSID: " << (const char*)conf.ap.ssid << '\n';
                out << "Visible: " << (conf.ap.ssid_hidden == 0 ? "Yes" : "No") << '\n';

                const char* mode;
                switch (conf.ap.authmode) {
                    case WIFI_AUTH_OPEN:
                        mode = "None";
                        break;
                    case WIFI_AUTH_WEP:
                        mode = "WEP";
                        break;
                    case WIFI_AUTH_WPA_PSK:
                        mode = "WPA";
                        break;
                    case WIFI_AUTH_WPA2_PSK:
                        mode = "WPA2";
                        break;
                    default:
                        mode = "WPA/WPA2";
                }

                out << "Authentication: " << mode << '\n';
                out << "Max Connections: " << String(conf.ap.max_connection) << '\n';

                tcpip_adapter_dhcp_status_t dhcp_status;
                tcpip_adapter_dhcps_get_status(TCPIP_ADAPTER_IF_AP, &dhcp_status);
                out << "DHCP Server: " << (dhcp_status == TCPIP_ADAPTER_DHCP_STARTED ? "Started" : "Stopped") << '\n';

                out << "IP: " << WiFi.softAPIP().toString() << '\n';

                tcpip_adapter_ip_info_t ip_AP;
                tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_AP);
                out << "Gateway: " << IPAddress(ip_AP.gw.addr).toString() << '\n';
                out << "Mask: " << IPAddress(ip_AP.netmask.addr).toString() << '\n';

                wifi_sta_list_t          station;
                tcpip_adapter_sta_list_t tcpip_sta_list;
                esp_wifi_ap_get_sta_list(&station);
                tcpip_adapter_get_sta_list(&station, &tcpip_sta_list);
                out << "Connected channels: " << String(station.num) << '\n';

                for (int i = 0; i < station.num; i++) {
                    out << wifi_config.mac2str(tcpip_sta_list.sta[i].mac);
                    out << " " << IPAddress(tcpip_sta_list.sta[i].ip.addr).toString() << '\n';
                }
                out << "Disabled Mode: ";
                print_mac("STA", WiFi.macAddress(), out);
                break;
            case WIFI_AP_STA:  //we should not be in this state but just in case ....
                out << "Mixed" << '\n';

                print_mac("STA", WiFi.macAddress(), out);
                print_mac("AP", WiFi.softAPmacAddress(), out);
                break;
            default:  //we should not be there if no wifi ....
                out << "Off" << '\n';
                break;
        }

        out << "Notifications: " << (notificationsservice.started() ? "Enabled" : "Disabled");
        if (notificationsservice.started()) {
            out << "(" << notificationsservice.getTypeString() << ")";
        }
        out << '\n';
    }

    String WiFiConfig::_hostname          = "";
    bool   WiFiConfig::_events_registered = false;

    WiFiConfig::WiFiConfig() {
        new WebCommand(NULL, WEBCMD, WU, "ESP410", "WiFi/ListAPs", listAPs);

        wifi_hostname = new StringSetting("Hostname",
                                          WEBSET,
                                          WA,
                                          "ESP112",
                                          "Hostname",
                                          DEFAULT_HOSTNAME,
                                          MIN_HOSTNAME_LENGTH,
                                          MAX_HOSTNAME_LENGTH,
                                          (bool (*)(char*))WiFiConfig::isHostnameValid);

        wifi_ap_channel =
            new IntSetting("AP Channel", WEBSET, WA, "ESP108", "AP/Channel", DEFAULT_AP_CHANNEL, MIN_CHANNEL, MAX_CHANNEL, NULL);
        wifi_ap_ip       = new IPaddrSetting("AP Static IP", WEBSET, WA, "ESP107", "AP/IP", DEFAULT_AP_IP, NULL);
        wifi_ap_password = new StringSetting("AP Password",
                                             WEBSET,
                                             WA,
                                             "ESP106",
                                             "AP/Password",
                                             DEFAULT_AP_PWD,
                                             MIN_PASSWORD_LENGTH,
                                             MAX_PASSWORD_LENGTH,
                                             (bool (*)(char*))WiFiConfig::isPasswordValid);
        wifi_ap_ssid     = new StringSetting(
            "AP SSID", WEBSET, WA, "ESP105", "AP/SSID", DEFAULT_AP_SSID, MIN_SSID_LENGTH, MAX_SSID_LENGTH, (bool (*)(char*))WiFiConfig::isSSIDValid);
        wifi_sta_netmask = new IPaddrSetting("Station Static Mask", WEBSET, WA, NULL, "Sta/Netmask", DEFAULT_STA_MK, NULL);
        wifi_sta_gateway = new IPaddrSetting("Station Static Gateway", WEBSET, WA, NULL, "Sta/Gateway", DEFAULT_STA_GW, NULL);
        wifi_sta_ip      = new IPaddrSetting("Station Static IP", WEBSET, WA, NULL, "Sta/IP", DEFAULT_STA_IP, NULL);
        wifi_sta_mode = new EnumSetting("Station IP Mode", WEBSET, WA, "ESP102", "Sta/IPMode", DEFAULT_STA_IP_MODE, &staModeOptions, NULL);
        wifi_sta_password = new StringSetting("Station Password",
                                              WEBSET,
                                              WA,
                                              "ESP101",
                                              "Sta/Password",
                                              DEFAULT_STA_PWD,
                                              MIN_PASSWORD_LENGTH,
                                              MAX_PASSWORD_LENGTH,
                                              (bool (*)(char*))WiFiConfig::isPasswordValid);
        wifi_sta_ssid     = new StringSetting("Station SSID",
                                          WEBSET,
                                          WA,
                                          "ESP100",
                                          "Sta/SSID",
                                          DEFAULT_STA_SSID,
                                          MIN_SSID_LENGTH,
                                          MAX_SSID_LENGTH,
                                          (bool (*)(char*))WiFiConfig::isSSIDValid);

        wifi_mode = new EnumSetting("WiFi mode", WEBSET, WA, "ESP116", "WiFi/Mode", WiFiFallback, &wifiModeOptions, NULL);

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

    String WiFiConfig::webInfo() {
        String s = " # webcommunication: Sync: ";
        s += String(web_server.port() + 1);
        s += ":";
        switch (WiFi.getMode()) {
            case WIFI_MODE_AP:
                s += WiFi.softAPIP().toString();
                break;
            case WIFI_MODE_STA:
                s += WiFi.localIP().toString();
                break;
            case WIFI_MODE_APSTA:
                s += WiFi.softAPIP().toString();
                break;
            default:
                s += "0.0.0.0";
                break;
        }
        s += " # hostname:";
        s += wifi_config.Hostname();
        if (WiFi.getMode() == WIFI_AP) {
            s += "(AP mode)";
        }
        return s;
    }

    String WiFiConfig::info() {
        static String result;
        String        tmp;

        if ((WiFi.getMode() == WIFI_MODE_STA) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            result += "Mode=STA:SSID=";
            result += WiFi.SSID();
            result += ":Status=";
            result += (WiFi.status() == WL_CONNECTED) ? "Connected" : "Not connected";
            result += ":IP=";
            result += WiFi.localIP().toString();
            result += ":MAC=";
            tmp = WiFi.macAddress();
            tmp.replace(":", "-");
            result += tmp;
        }

        if ((WiFi.getMode() == WIFI_MODE_AP) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            if (WiFi.getMode() == WIFI_MODE_APSTA) {
                result += "]\n[MSG:";
            }
            result += "Mode=AP:SSDI=";
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            result += (const char*)conf.ap.ssid;
            result += ":IP=";
            result += WiFi.softAPIP().toString();
            result += ":MAC=";
            tmp = WiFi.softAPmacAddress();
            tmp.replace(":", "-");
            result += tmp;
        }
        if (WiFi.getMode() == WIFI_MODE_NULL) {
            result += "No Wifi";
        }
        return result;
    }

    /**
     * Helper to convert  IP string to int
     */

    uint32_t WiFiConfig::IP_int_from_string(String& s) {
        uint32_t  ip_int = 0;
        IPAddress ipaddr;
        if (ipaddr.fromString(s)) {
            ip_int = ipaddr;
        }
        return ip_int;
    }

    /**
     * Helper to convert int to IP string
     */

    String WiFiConfig::IP_string_from_int(uint32_t ip_int) {
        IPAddress ipaddr(ip_int);
        return ipaddr.toString();
    }

    /**
     * Check if Hostname string is valid
     */

    bool WiFiConfig::isHostnameValid(const char* hostname) {
        //limited size
        if (!hostname) {
            return true;
        }
        char c;
        // length is checked automatically by string setting
        //only letter and digit
        for (int i = 0; i < strlen(hostname); i++) {
            c = hostname[i];
            if (!(isdigit(c) || isalpha(c) || c == '-')) {
                return false;
            }
            if (c == ' ') {
                return false;
            }
        }
        return true;
    }

    /**
     * Check if SSID string is valid
     */

    bool WiFiConfig::isSSIDValid(const char* ssid) {
        //limited size
        //char c;
        // length is checked automatically by string setting
        //only printable
        if (!ssid) {
            return true;
        }
        for (int i = 0; i < strlen(ssid); i++) {
            if (!isPrintable(ssid[i])) {
                return false;
            }
        }
        return true;
    }

    /**
     * Check if password string is valid
     */

    bool WiFiConfig::isPasswordValid(const char* password) {
        if (!password) {
            return true;
        }
        if (strlen(password) == 0) {
            return true;  //open network
        }

        // Limited size. Length is checked automatically by string setting

        return true;
    }

    /**
     * Check if IP string is valid
     */
    bool WiFiConfig::isValidIP(const char* string) {
        IPAddress ip;
        return ip.fromString(string);
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
        String  msg, msg_out;
        uint8_t dot = 0;
        for (size_t i = 0; i < 10; ++i) {
            switch (WiFi.status()) {
                case WL_NO_SSID_AVAIL:
                    log_info("No SSID");
                    return false;
                case WL_CONNECT_FAILED:
                    log_info("Connection failed");
                    return false;
                case WL_CONNECTED:
                    log_info("Connected - IP is " << WiFi.localIP().toString());
                    display("IP", WiFi.localIP().toString());
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
            COMMANDS::wait(2000);
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
        //SSID
        String SSID = wifi_sta_ssid->get();
        if (SSID.length() == 0) {
            log_info("STA SSID is not set");
            return false;
        }
        WiFi.mode(WIFI_STA);
        //Get parameters for STA
        String h = wifi_hostname->get();
        WiFi.setHostname(h.c_str());
        //password
        String  password = wifi_sta_password->get();
        int8_t  IP_mode  = wifi_sta_mode->get();
        int32_t IP       = wifi_sta_ip->get();
        int32_t GW       = wifi_sta_gateway->get();
        int32_t MK       = wifi_sta_netmask->get();
        //if not DHCP
        if (IP_mode != DHCP_MODE) {
            IPAddress ip(IP), mask(MK), gateway(GW);
            WiFi.config(ip, gateway, mask);
        }
        if (WiFi.begin(SSID.c_str(), (password.length() > 0) ? password.c_str() : NULL)) {
            log_info("Connecting to STA SSID:" << SSID.c_str());
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

        //auto comms = config->_comms;  // _comms is automatically created in afterParse
        //auto ap    = comms->_apConfig;
        // ap might be nullpt if there is an explicit comms: with no wifi_ap:
        // If a _comms node is created automatically, a default AP config is created too
        // if (!ap) {
        //     return false;
        // }

        //Get parameters for AP
        //SSID
        String SSID = wifi_ap_ssid->get();
        if (SSID.length() == 0) {
            SSID = DEFAULT_AP_SSID;
        }

        String password = wifi_ap_password->get();

        int8_t channel = int8_t(wifi_ap_channel->get());
        if (channel == 0) {
            channel = DEFAULT_AP_CHANNEL;
        }

        IPAddress ip(wifi_ap_ip->get());
        IPAddress mask;
        mask.fromString(DEFAULT_AP_MK);

        log_info("AP SSID " << SSID << " IP " << ip.toString() << " mask " << mask.toString() << " channel " << channel);

        //Set static IP
        WiFi.softAPConfig(ip, ip, mask);

        //Start AP
        if (WiFi.softAP(SSID.c_str(), (password.length() > 0) ? password.c_str() : NULL, channel)) {
            log_info("AP started");
            display("IP", ip.toString());
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
        for (Setting* s = Setting::List; s; s = s->next()) {
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
    void WiFiConfig::handle() {
        //Services
        COMMANDS::wait(0);
        wifi_services.handle();
    }

    Error WiFiConfig::listAPs(char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP410
        JSONencoder j(false, out);
        j.begin();
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
                    j.member("SSID", WiFi.SSID(i));
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
        out << '\n';
        return Error::Ok;
    }

    WiFiConfig::~WiFiConfig() { end(); }
}
#endif
