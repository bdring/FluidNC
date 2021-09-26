// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Settings.h"

enum WiFiStartupMode {
    WiFiOff = 0,
    WiFiSTA,
    WiFiAP,
    WiFiFallback,  // Try STA and fall back to AP if STA fails
};

namespace WebUI {
#ifdef ENABLE_AUTHENTICATION
    extern StringSetting* user_password;
    extern StringSetting* admin_password;
#endif

#ifdef ENABLE_WIFI

    extern EnumSetting*   wifi_mode;
    extern EnumSetting*   wifi_sta_mode;
    extern IPaddrSetting* wifi_sta_ip;
    extern IPaddrSetting* wifi_sta_gateway;
    extern IPaddrSetting* wifi_sta_netmask;

    extern StringSetting* wifi_sta_ssid;
    extern StringSetting* wifi_ap_ssid;

    extern IPaddrSetting* wifi_ap_ip;

    extern IntSetting* wifi_ap_channel;

    extern StringSetting* wifi_hostname;
    extern EnumSetting*   http_enable;
    extern IntSetting*    http_port;
    extern EnumSetting*   telnet_enable;
    extern IntSetting*    telnet_port;

    extern StringSetting* wifi_sta_password;
    extern StringSetting* wifi_ap_password;

    extern EnumSetting*   notification_type;
    extern StringSetting* notification_t1;
    extern StringSetting* notification_t2;
    extern StringSetting* notification_ts;
#endif

#ifdef ENABLE_BLUETOOTH
    extern EnumSetting*   bt_enable;
    extern StringSetting* bt_name;
#endif
}
