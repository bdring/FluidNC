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

    bool COMMANDS::isLocalPasswordValid(char* password) {
        if (!password) {
            return true;
        }
        char c;
        //limited size
        if ((strlen(password) > MAX_LOCAL_PASSWORD_LENGTH) || (strlen(password) < MIN_LOCAL_PASSWORD_LENGTH)) {
            return false;
        }

        //no space allowed
        for (size_t i = 0; i < strlen(password); i++) {
            c = password[i];
            if (c == ' ') {
                return false;
            }
        }
        return true;
    }

    void COMMANDS::send_json_command_response(Channel& out, uint cmdID, bool isok, std::string message) {
        JSONencoder j(true, &out);
        j.begin();
        j.member("cmd", String(cmdID).c_str());
        j.member("status", isok ? "ok" : "error");
        j.member("data", message);
        j.end();
    }

    const char* COMMANDS::get_param(const char* cmd_params, const char* label) {
        static String res;
        res           = "";
        int    start  = 1;
        int    end    = -1;
        String tmp    = "";
        String slabel = " ";
        res           = cmd_params;
        res.replace("\r ", "");
        res.replace("\n ", "");
        res.trim();
        if (res.length() == 0) {
            return res.c_str();
        }

        tmp = " " + res;
        slabel += label;
        if (strlen(label) > 0) {
            start = tmp.indexOf(slabel);
            if (start == -1) {
                return "";
            }
            start += slabel.length();
            end = get_space_pos(tmp.c_str(), start);
        }
        if (end == -1) {
            end = tmp.length();
        }
        //extract parameter
        res = tmp.substring(start, end);

        //remove space format
        res.replace("\\ ", " ");
        //be sure no extra space
        res.trim();
        return res.c_str();
    }

    bool COMMANDS::has_tag(const char* cmd_params, const char* tag) {
        String tmp  = "";
        String stag = " ";
        if ((strlen(cmd_params) == 0) || (strlen(tag) == 0)) {
            return false;
        }
        stag += tag;
        tmp = cmd_params;
        tmp.trim();
        tmp = " " + tmp;
        if (tmp.indexOf(stag) == -1) {
            return false;
        }
        //to support plain , plain=yes , plain=no
        String param = String(tag) + "=";
        String parameter = get_param(cmd_params, param.c_str());
        if (parameter.length() != 0) {
            if (parameter == "YES" || parameter == "true" || parameter == "TRUE" || parameter == "yes" || parameter == "1") {
                return true;
            }

            return false;
        }

        return true;
    }

    //find space in string
    //if space is has \ before it is ignored
    int COMMANDS::get_space_pos(const char* string, uint from) {
        uint len = strlen(string);
        if (len < from) {
            return -1;
        }
        for (uint i = from; i < len; i++) {
            if (string[i] == ' ') {
                //if it is first char
                if (i == from) {
                    return from;
                }
                //if not first one and previous char is not '\'
                if (string[i - 1] != '\\') {
                    return (i);
                }
            }
        }
        return -1;
    }

    /**
     * Restart ESP
     */
    void COMMANDS::restart_MCU() {
        _restart_MCU = true;
    }

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
