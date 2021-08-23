/*
  Commands.cpp - GRBL_ESP command class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
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
        esp_task_wdt_reset();  //for a wait 0;
        //wait feeding WDT
        while ((millis() - start_time) < milliseconds) {
            esp_task_wdt_reset();
        }
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
