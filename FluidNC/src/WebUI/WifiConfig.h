// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

//Preferences entries

#include "../Config.h"       // ENABLE_*
#include "../Channel.h"      // Channel
#include "../Error.h"        // Error
#include "Authentication.h"  // AuthenticationLevel

#include <WString.h>

#ifndef ENABLE_WIFI
namespace WebUI {
    class WiFiConfig {
    public:
        static String webInfo() { return String(); }
        static String info() { return String(); }
        static bool   isPasswordValid(const char* password) { return false; }
        static bool   begin() { return false; }
        static void   end() {}
        static void   reset() {}
        static void   reset_settings() {}
        static void   handle() {}
        static bool   isOn() { return false; }
        static void   showWifiStats(Channel& out) {}
    };
    extern WiFiConfig wifi_config;
}
#else
#    include <WiFi.h>
#    include "../Settings.h"

namespace WebUI {
    extern StringSetting* wifi_hostname;

    // TODO: Clean these constants up. Some of them don't belong here.

    static const int DHCP_MODE   = 0;
    static const int STATIC_MODE = 1;

    //defaults values
    static const char* DEFAULT_HOSTNAME   = "fluidnc";
    static const char* DEFAULT_STA_SSID   = "";
    static const char* DEFAULT_STA_PWD    = "";
    static const char* DEFAULT_STA_IP     = "0.0.0.0";
    static const char* DEFAULT_STA_GW     = "0.0.0.0";
    static const char* DEFAULT_STA_MK     = "0.0.0.0";
    static const char* DEFAULT_AP_SSID    = "FluidNC";
    static const char* DEFAULT_AP_PWD     = "12345678";
    static const char* DEFAULT_AP_IP      = "192.168.0.1";
    static const char* DEFAULT_AP_MK      = "255.255.255.0";
    static const int   DEFAULT_AP_CHANNEL = 1;

    static const int   DEFAULT_STA_IP_MODE = DHCP_MODE;
    static const char* HIDDEN_PASSWORD     = "********";

    //boundaries
    static const int MAX_SSID_LENGTH     = 32;
    static const int MIN_SSID_LENGTH     = 0;  // Allow null SSIDs as a way to disable
    static const int MAX_PASSWORD_LENGTH = 64;
    //min size of password is 0 or upper than 8 char
    //so let set min is 8
    static const int MIN_PASSWORD_LENGTH = 8;
    static const int MAX_HOSTNAME_LENGTH = 32;
    static const int MIN_HOSTNAME_LENGTH = 1;
    static const int MIN_CHANNEL         = 1;
    static const int MAX_CHANNEL         = 14;

    class WiFiConfig {
    public:
        WiFiConfig();

        static void reset();

        static String   webInfo();
        static String   info();
        static bool     isValidIP(const char* string);
        static bool     isPasswordValid(const char* password);
        static bool     isSSIDValid(const char* ssid);
        static bool     isHostnameValid(const char* hostname);
        static uint32_t IP_int_from_string(String& s);
        static String   IP_string_from_int(uint32_t ip_int);
        static String   Hostname() { return _hostname; }
        static char*    mac2str(uint8_t mac[8]);
        static bool     StartAP();
        static bool     StartSTA();
        static void     StopWiFi();
        static int32_t  getSignal(int32_t RSSI);
        static bool     begin();
        static void     end();
        static void     handle();
        static void     reset_settings();
        static bool     isOn();

        static Error listAPs(char* parameter, AuthenticationLevel auth_level, Channel& out);
        static void  showWifiStats(Channel& out);

        ~WiFiConfig();

    private:
        static bool   ConnectSTA2AP();
        static void   WiFiEvent(WiFiEvent_t event);
        static String _hostname;
        static bool   _events_registered;
    };

    extern WiFiConfig wifi_config;

    extern EnumSetting* wifi_mode;

    extern StringSetting* wifi_sta_ssid;
    extern StringSetting* wifi_sta_password;

    extern EnumSetting*   wifi_sta_mode;
    extern IPaddrSetting* wifi_sta_ip;
    extern IPaddrSetting* wifi_sta_gateway;
    extern IPaddrSetting* wifi_sta_netmask;

    extern StringSetting* wifi_ap_ssid;
    extern StringSetting* wifi_ap_password;

    extern IPaddrSetting* wifi_ap_ip;

    extern IntSetting* wifi_ap_channel;

    extern StringSetting* wifi_hostname;
}
#endif
