// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Commands.h"
#include <esp32-hal.h>  // millis()
#include <Esp.h>        // ESP.restart()

#include "Authentication.h"  // MAX_LOCAL_PASSWORD_LENGTH

#include <esp_err.h>
#include <cstring>

namespace WebUI {
    bool COMMANDS::_restart_MCU = false;
    bool COMMANDS::_hibernate_MCU = false;

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
    void COMMANDS::restart_MCU() { _restart_MCU = true; }

    /**
     * hibernate
     */
    void COMMANDS::hibernate_MCU() { _hibernate_MCU = true; }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void COMMANDS::handle() {
        if (_restart_MCU) {
            ESP.restart();
            while (1) {}
        } else {
            if (_hibernate_MCU) {
                // now we go to deep sleep (hibernate)
                esp_sleep_pd_config(ESP_PD_DOMAIN_MAX, ESP_PD_OPTION_OFF);
                esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,   ESP_PD_OPTION_OFF);
                esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
                esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
                esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL,         ESP_PD_OPTION_OFF);

                uint64_t sleep_time = 31536000 * 1000000ULL; // for one year
                esp_sleep_enable_timer_wakeup(sleep_time);
                esp_deep_sleep_start();

                // we should never get here
                ESP.restart();
                while (1) {}
            }
        }
    }
}
