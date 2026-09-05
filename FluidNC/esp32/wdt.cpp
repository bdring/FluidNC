// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "wdt.h"
#include "esp_task_wdt.h"
#include <freertos/FreeRTOS.h>
#include "Config.h"
#include <esp_idf_version.h>

// ESP-IDF v5 names this CONFIG_ESP_TASK_WDT_EN; v4.x names it
// CONFIG_ESP_TASK_WDT.  Testing only the v5 name leaves every function in this
// file compiled to an empty body on v4.x, so feed_watchdog() feeds nothing and
// add_watchdog_to_task() subscribes nothing, while the task watchdog itself is
// enabled and will panic on expiry.  Accept either name.
#if defined(CONFIG_ESP_TASK_WDT_EN) || defined(CONFIG_ESP_TASK_WDT)
#    define FLUIDNC_TASK_WDT_ENABLED 1
#endif

static TaskHandle_t wdt_task_handle = nullptr;

static void get_wdt_task_handle() {
#if ESP_IDF_VERSION_MAJOR >= 5 && ESP_IDF_VERSION_MINOR >= 2
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCore(0);
#else
    TaskHandle_t idle_0 = xTaskGetIdleTaskHandleForCPU(0);
#endif
    esp_err_t err;
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

// cppcheck-suppress unusedFunction
void enable_core0_WDT() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    if (!wdt_task_handle) {
        return;
    }
    esp_err_t err;
    if ((err = esp_task_wdt_add(wdt_task_handle)) != ESP_OK) {
        log_error("Failed to add Core 0 IDLE task to WDT " << err);
    }
#endif
}

// cppcheck-suppress unusedFunction
void disable_core0_WDT() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    get_wdt_task_handle();
    if (!wdt_task_handle) {
        return;
    }
    esp_err_t err;
    if ((err = esp_task_wdt_delete(wdt_task_handle)) != ESP_OK) {
        log_error("Failed to remove Core 0 IDLE task from WDT " << err);
    }
#endif
}

void feed_watchdog() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    // esp_task_wdt_reset() logs an error ("task not found") if the current
    // task isn't subscribed to the TWDT, instead of silently no-opping.
    // FluidNC's watchdog is opt-in (see add_watchdog_to_task()), and several
    // call sites call feed_watchdog() defensively from tasks that may or may
    // not be subscribed (e.g. loopTask, which platform_preinit() deliberately
    // unsubscribes - see esp32/esp32s3/Platform.h). Check first so those
    // defensive calls are actually silent, as intended, instead of spamming
    // the log with harmless "task not found" errors.
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();
    }
#endif
}

void add_watchdog_to_task() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    esp_task_wdt_add(NULL);  // NULL means current task
#endif
}
