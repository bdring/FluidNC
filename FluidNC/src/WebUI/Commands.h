// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "../Config.h"

namespace WebUI {
    class COMMANDS {
    public:
        static void handle();
        static void restart_MCU();
        static bool isLocalPasswordValid(char* password);

    private:
        static bool _restart_MCU;
    };
}
