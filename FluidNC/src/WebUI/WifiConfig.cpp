// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WifiConfig.h"

#include "../Machine/MachineConfig.h"

WebUI::WiFiConfig wifi_config;

#ifdef ENABLE_WIFI
#    include "../Main.h"   // display()
#    include "Commands.h"  // COMMANDS
#    include "WifiServices.h"
#    include "WebSettings.h"

#    include <WiFi.h>
#    include <esp_wifi.h>
#    include <ESPmDNS.h>
#    include <FS.h>
#    include <SPIFFS.h>
#    include <cstring>

namespace WebUI {
    String WiFiConfig::_hostname          = "";
    bool   WiFiConfig::_events_registered = false;

    WiFiConfig::WiFiConfig() {}

    //just simple helper to convert mac address to string
    char* WiFiConfig::mac2str(uint8_t mac[8]) {
        static char macstr[18];
        if (0 > sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5])) {
            strcpy(macstr, "00:00:00:00:00:00");
        }
        return macstr;
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
                result += "]\r\n[MSG:";
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

    void WiFiConfig::StopWiFi() {
        //Sanity check
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
    bool WiFiConfig::Is_WiFi_on() { return !(WiFi.getMode() == WIFI_MODE_NULL); }

    /**
     * Handle not critical actions that must be done in sync environment
     */
    void WiFiConfig::handle() {
        //Services
        COMMANDS::wait(0);
        wifi_services.handle();
    }

    WiFiConfig::~WiFiConfig() { end(); }
}
#endif
