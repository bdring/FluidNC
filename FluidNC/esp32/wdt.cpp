// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "wdt.h"
#include "esp_task_wdt.h"
#include <freertos/FreeRTOS.h>
#include "src/Config.h"

static TaskHandle_t wdt_task_handle = nullptr;

static void get_wdt_task_handle() {
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    esp_err_t    err;
    err = esp_task_wdt_status(idle_0);
    switch (err) {
        case ESP_OK:
            wdt_task_handle = idle_0;
            break;
        case ESP_ERR_NOT_FOUND:
            wdt_task_handle = nullptr;
            return;
        case ESP_ERR_INVALID_STATE:
            wdt_task_handle = nullptr;
            return;
    }
}

void enable_core0_WDT() {
    if (!wdt_task_handle) {
        return;
    }
    esp_err_t err;
    if ((err = esp_task_wdt_add(wdt_task_handle)) != ESP_OK) {
        log_error("Failed to add Core 0 IDLE task to WDT " << err);
    }
}

void disable_core0_WDT() {
    get_wdt_task_handle();
    if (!wdt_task_handle) {
        return;
    }
    esp_err_t err;
    if ((err = esp_task_wdt_delete(wdt_task_handle)) != ESP_OK) {
        log_error("Failed to remove Core 0 IDLE task from WDT " << err);
    }
}
