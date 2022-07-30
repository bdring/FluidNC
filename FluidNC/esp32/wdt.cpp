// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "wdt.h"
#include "esp_task_wdt.h"
#include <freertos/FreeRTOS.h>
#include "src/Logging.h"

void enable_core0_WDT() {
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if (idle_0 == NULL || esp_task_wdt_add(idle_0) != ESP_OK) {
        log_error("Failed to add Core 0 IDLE task to WDT");
    }
}

void disable_core0_WDT() {
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
    if (idle_0 == NULL || esp_task_wdt_delete(idle_0) != ESP_OK) {
        log_error("Failed to remove Core 0 IDLE task from WDT");
    }
}
