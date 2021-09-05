// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Commands.h"
#include <esp32-hal.h>  // millis()
#include <Esp.h>        // ESP.restart()

#include "Authentication.h"  // MAX_LOCAL_PASSWORD_LENGTH

#include <esp_err.h>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t esp_task_wdt_reset();
#ifdef __cplusplus
}
#endif

namespace WebUI {
    bool COMMANDS::restart_ESP_module = false;

    /*
     * delay is to avoid with asyncwebserver and may need to wait sometimes
     */
    void COMMANDS::wait(uint32_t milliseconds) {
        uint32_t start_time = millis();
        //wait feeding WDT
        do {
            esp_task_wdt_reset();
        } while ((millis() - start_time) < milliseconds);
    }
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

    /**
     * Restart ESP
     */
    void COMMANDS::restart_ESP() { restart_ESP_module = true; }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void COMMANDS::handle() {
        COMMANDS::wait(0);
        //in case of restart requested
        if (restart_ESP_module) {
            ESP.restart();
            while (1) {}
        }
    }
}
