#pragma once

#include <soc/soc_caps.h>

#define WEAK_LINK __attribute__((weak))
#define WITH_MBEDTLS
#define HAVE_UPDATE

#define MAX_N_UARTS (SOC_UART_NUM + MAX_N_USB_HOST)
#define MAX_N_I2C SOC_I2C_NUM
#define MAX_N_ETH 1  // W5500 etc. via Arduino-ESP32 core 3.x ETH.h; see Machine/EthPhy.h
#define MAX_N_USB_HOST 1

// The number that we support, regardless of how many the chip has
#define MAX_N_DACS 0
#define MAX_N_RMT 0
#define MAX_N_I2SO 1
#define MAX_N_SPI 1
#define MAX_N_SDCARD 1
#define MAX_N_SIMULATOR 0

#define MAX_N_GPIO SOC_GPIO_PIN_COUNT /* 49 */
#define DEFAULT_STEPPING_ENGINE Stepping::TIMED

#define STEPPING_FREQUENCY 20000000

// Serial baud rate
// The ESP32 boot text is 115200, so you will not see early startup
// messages from the ESP32 bootloader if you use a different baud rate,
// and some serial monitor programs that assume 115200 might not work.
const int BAUD_RATE = 115200;

#define LAST_ERROR lastError

#include <esp_task_wdt.h>
#include <esp_log.h>

#include "esp32-hal.h"  // disableCore0WDT

#include <esp_idf_version.h>

#include "Logging.h"

// As of ESP-IDF 5.4, the Arduino core's Wire/TwoWire implementation
// (esp32-hal-i2c-ng.c) switched to the new "driver_ng" I2C driver
// (driver/i2c_master.h). ESP-IDF now aborts at startup if the legacy
// driver (driver/i2c.h, used by esp32/i2c.cpp) is also installed
// anywhere in the same firmware ("CONFLICT! driver_ng is not allowed
// to be used with this old driver"). Route FluidNC's own I2C bus
// through Wire (arduino_i2c_driver.cpp) so everything uses driver_ng.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
#    define USE_ARDUINO_I2C_DRIVER 1
#endif

inline void platform_preinit() {
#if ESP_IDF_VERSION_MAJOR >= 5
    // esp_littlefs logs its own ESP_LOGE for a condition FluidNC treats as
    // normal: localfs_mount()/localfs_format() intentionally probe a
    // partition named "littlefs" first, then fall back to a partition named
    // "spiffs" (see esp32/localfs.cpp); the first probe logs a "partition
    // could not be found" error even though the fallback succeeds.
    // Suppress at the IDF logging level rather than trying to intercept
    // FluidNC's own logger, since this comes from inside the IDF component.
    //
    // Deliberately NOT suppressing the "task_wdt" tag here: TWDT timeout
    // panics/backtraces are logged on that same tag (task_wdt.c uses one
    // TAG for everything), so blanket-suppressing it would also hide real
    // watchdog crash reports -- which is effectively what happened while
    // this code still called esp_task_wdt_add(NULL) below and suppressed
    // "task_wdt" logging at the same time.
    esp_log_level_set("esp_littlefs", ESP_LOG_NONE);
#endif

#if ESP_IDF_VERSION_MAJOR < 5
    disableCore0WDT();
#else
    // The loop task (running setup()/loop()) must NOT be subscribed to the
    // TWDT. FluidNC's watchdog design (see esp32/wdt.cpp, Driver/watchdog.h)
    // is opt-in: only tasks that explicitly call add_watchdog_to_task() and
    // then periodically feed_watchdog() are meant to be monitored. setup()
    // legitimately blocks for long stretches during network bring-up --
    // WifiConfig's STA connect retries for up to ~40s, and EthConfig's
    // link-up wait blocks for up to 5s -- without feeding any watchdog.
    //
    // As of Arduino-ESP32 core 3.x, the framework's own loopTask startup
    // subscribes the loop task to the TWDT before setup() runs (that's what
    // caused the "task is already subscribed" log line previously seen
    // here). That subscription is exactly what was tripping a real TWDT
    // timeout -- and killing the board -- during EthConfig's blocking
    // Ethernet link-up wait, which doesn't feed the watchdog. So instead of
    // adding a second subscription, actively remove whatever subscription
    // the framework already created.
    esp_err_t err = esp_task_wdt_delete(NULL);  // NULL means current task
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        log_error("esp_task_wdt_delete failed: " << esp_err_to_name(err));
    }
#endif
}

inline bool should_exit() {
    return false;
}

inline BaseType_t xTaskCreateAffinitySet(TaskFunction_t      pvTaskCode,
                                         const char* const   pcName,
                                         const uint32_t      usStackDepth,
                                         void* const         pvParameters,
                                         UBaseType_t         uxPriority,
                                         int                 affinityMask,
                                         TaskHandle_t* const pvCreatedTask) {
    BaseType_t core = tskNO_AFFINITY;
    if (affinityMask & 0x1) {
        core = 0;
    } else if (affinityMask & 0x2) {
        core = 1;
    }
    return xTaskCreateUniversal(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pvCreatedTask, core);
}
