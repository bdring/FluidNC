#pragma once

#include <esp_attr.h>
#include <esp_compiler.h>
#include <soc/soc_caps.h>

#define IRAM IRAM_ATTR
#define INLINE inline __attribute__((always_inline))

#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))

#define WEAK_LINK __attribute__((weak))
#define WITH_MBEDTLS
#define HAVE_UPDATE

#define MAX_N_UARTS SOC_UART_NUM
#define MAX_N_I2C SOC_I2C_NUM
#define MAX_N_USB_HOST 0
#define MAX_N_DACS SOC_DAC_PERIPH_NUM
#define MAX_N_RMT SOC_RMT_GROUPS

// The number that we support, regardless of how many the chip has
#define MAX_N_I2SO 1
#define MAX_N_SPI 1
#define MAX_N_SDCARD 1
#define MAX_N_SIMULATOR 0

#define MAX_N_GPIO SOC_GPIO_PIN_COUNT /* 40 */
#define DEFAULT_STEPPING_ENGINE Stepping::RMT_ENGINE

#define STEPPING_FREQUENCY 20000000

// Serial baud rate
// The ESP32 boot text is 115200, so you will not see early startup
// messages from the ESP32 bootloader if you use a different baud rate,
// and some serial monitor programs that assume 115200 might not work.
const int BAUD_RATE = 115200;

#define LAST_ERROR lastError

#include <esp_task_wdt.h>

#include "esp32-hal.h"  // disableCore0WDT

#include <esp_idf_version.h>

#include "Logging.h"

inline void platform_preinit() {
#if ESP_IDF_VERSION_MAJOR < 5
    disableCore0WDT();
#else
    // The loop task (running setup()/loop()) must NOT be subscribed to the
    // TWDT here. FluidNC's watchdog design (see esp32/wdt.cpp,
    // Driver/watchdog.h) is opt-in: only tasks that explicitly call
    // add_watchdog_to_task() and then periodically feed_watchdog() are meant
    // to be monitored. setup() legitimately blocks for long stretches during
    // network bring-up -- WifiConfig's STA connect retries for up to ~40s,
    // and EthConfig's link-up wait blocks for up to 5s -- without feeding
    // any watchdog.
    //
    // This branch previously called esp_task_wdt_add(NULL) here, which is
    // exactly the bug found and fixed on ESP32-S3 (see
    // esp32/esp32s3/Platform.h): as of Arduino-ESP32 core 3.x / ESP-IDF 5.x,
    // subscribing the loop task this early tripped a real TWDT timeout --
    // and killed the board -- during blocking network bring-up. This code
    // path is currently unreachable on plain ESP32 (which is pinned to
    // ESP-IDF 4.4, taking the branch above), but is fixed preemptively so it
    // doesn't silently reintroduce that crash if plain ESP32 is ever moved
    // to ESP-IDF 5.x, matching what already happened for S3.
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_err_t err = esp_task_wdt_delete(NULL);  // NULL means current task
        if (err != ESP_OK) {
            log_error("esp_task_wdt_delete failed: " << esp_err_to_name(err));
        }
    }
#endif
}

inline bool should_exit() {
    return false;
}

#define USE_ARDUINO_I2C_DRIVER 0

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
