#pragma once

/*
  WebSettings.h - Definitions for WebUI-related settings.

  Copyright (c) 2020 Mitch Bradley
  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

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
