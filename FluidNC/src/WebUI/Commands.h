// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include "../Config.h"

namespace WebUI {
    class COMMANDS {
    public:
        static void wait(uint32_t milliseconds);
        static void handle();
        static void restart_ESP();
        static bool isLocalPasswordValid(char* password);

    private:
        static bool restart_ESP_module;
    };
}
