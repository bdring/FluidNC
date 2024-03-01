// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Commands.h"
#include <esp32-hal.h>  // millis()
#include <Esp.h>        // ESP.restart()

#include "Authentication.h"  // MAX_LOCAL_PASSWORD_LENGTH
#include "../Configuration/JsonGenerator.h"

#include <esp_err.h>
#include <cstring>

namespace WebUI {
    bool COMMANDS::_restart_MCU = false;

    void COMMANDS::send_json_command_response(Channel& out, uint cmdID, bool isok, std::string message) {
        JSONencoder j(true, &out);
        j.begin();
        j.member("cmd", String(cmdID).c_str());
        j.member("status", isok ? "ok" : "error");
        j.member("data", message);
        j.end();
    }

    bool COMMANDS::isJSON(const char* cmd_params) { return strstr(cmd_params, "json=yes") != NULL; }

    /**
     * Restart ESP
     */
    void COMMANDS::restart_MCU() { _restart_MCU = true; }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void COMMANDS::handle() {
        if (_restart_MCU) {
            ESP.restart();
            while (1) {}
        }
    }
}
