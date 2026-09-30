// Copyright 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "wdt.h"
#include "Driver/watchdog.h"
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

// A task watchdog trip reports but does not reboot.  A reset cannot bring a
// CNC controller back to a usable state - position, job and spindle state are
// all lost - and every trip seen in the field so far has been a legitimate
// long wait rather than a hang.  The timeout is long enough that ordinary slow
// operations (VFD spinup, TLS handshakes, flash erase) stay clear of it.
// The interrupt watchdog is unchanged and still resets on a real lockup.
static const uint32_t task_wdt_timeout_s = 30;

// Set by the TWDT interrupt, reported later from task context by
// report_watchdog_timeouts().  The interrupt handler cannot log through
// FluidNC's channels; the IDF prints the stalled task names and backtraces to
// the UART console itself.
static volatile uint32_t wdt_timeouts = 0;

// Overrides the IDF's weak hook, called from the TWDT interrupt.  On IDF 4.x
// it runs with the TWDT spinlock held, so it must not do anything but count.
extern "C" void esp_task_wdt_isr_user_handler(void) {
    wdt_timeouts = wdt_timeouts + 1;
}

void configure_task_watchdog() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
#    if ESP_IDF_VERSION_MAJOR >= 5
    uint32_t idle_core_mask = 0;
#        ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
    idle_core_mask |= 1 << 0;
#        endif
#        ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
    idle_core_mask |= 1 << 1;
#        endif
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms     = task_wdt_timeout_s * 1000,
        .idle_core_mask = idle_core_mask,
        .trigger_panic  = false,
    };
    esp_err_t err = esp_task_wdt_reconfigure(&twdt_config);
#    else
    // On IDF 4.x, init on an already-initialized TWDT reconfigures it.
    esp_err_t err = esp_task_wdt_init(task_wdt_timeout_s, false);
#    endif
    if (err != ESP_OK) {
        log_error("Task watchdog reconfigure failed: " << esp_err_to_name(err));
    }
#endif
}

void report_watchdog_timeouts() {
    static uint32_t reported = 0;
    uint32_t        timeouts = wdt_timeouts;
    if (timeouts != reported) {
        log_warn("Task watchdog: a task went more than " << task_wdt_timeout_s << " s without feeding the watchdog ("
                                                         << (timeouts - reported) << "x); backtrace on the serial console");
        reported = timeouts;
    }
}

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

// Tracks whether this task was subscribed before we suspended it, so resume
// does not add a task that was never watched in the first place.
static thread_local bool wdt_was_subscribed = false;

void suspend_watchdog_for_task() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    wdt_was_subscribed = esp_task_wdt_status(NULL) == ESP_OK;
    if (wdt_was_subscribed) {
        esp_task_wdt_delete(NULL);
    }
#endif
}

void resume_watchdog_for_task() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    if (wdt_was_subscribed) {
        esp_task_wdt_add(NULL);
        esp_task_wdt_reset();
        wdt_was_subscribed = false;
    }
#endif
}

void add_watchdog_to_task() {
#ifdef FLUIDNC_TASK_WDT_ENABLED
    esp_task_wdt_add(NULL);  // NULL means current task
#endif
}
