// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Settings.h"

namespace WebUI {
    bool  split_params(char* parameter);
    char* get_param(const char* key, bool allowSpaces);

#ifdef ENABLE_AUTHENTICATION
    extern StringSetting* user_password;
    extern StringSetting* admin_password;
#endif

}
