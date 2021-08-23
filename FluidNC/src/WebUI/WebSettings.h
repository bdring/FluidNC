// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Settings.h"

namespace WebUI {
#ifdef ENABLE_AUTHENTICATION
    extern StringSetting* user_password;
    extern StringSetting* admin_password;
#endif

#ifdef ENABLE_WIFI
    extern StringSetting* wifi_sta_password;
    extern StringSetting* wifi_ap_password;

    extern EnumSetting*   notification_type;
    extern StringSetting* notification_t1;
    extern StringSetting* notification_t2;
    extern StringSetting* notification_ts;
#endif
}
