// Copyright (c) 2020 Mitch Bradley
// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../Settings.h"
#include <string>

namespace WebUI {
    bool get_param(const char* parameter, const char* key, std::string& s);

#ifdef ENABLE_AUTHENTICATION
    extern AuthPasswordSetting* user_password;
    extern AuthPasswordSetting* admin_password;
#endif

}
