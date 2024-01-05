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
        static void send_json_command_response(Channel& out, uint cmdID, bool isok=true, std::string message="");
        static bool has_tag (const char * cmd_params, const char * tag);
        static const char* get_param (const char * cmd_params, const char * label);
        static int get_space_pos(const char * string, uint from = 0);
    private:
        static bool _restart_MCU;
    };
}
