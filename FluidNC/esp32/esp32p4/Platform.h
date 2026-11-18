#pragma once

#include <soc/soc_caps.h>

#define WEAK_LINK __attribute__((weak))
#define WITH_MBEDTLS
#define HAVE_UPDATE

#define IRAM IRAM_ATTR
#define INLINE inline __attribute__((always_inline))
#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))

#define MAX_N_UARTS SOC_UART_NUM
#define MAX_N_I2C SOC_I2C_NUM
#define MAX_N_ETH 1
#define MAX_N_USB_HOST 0

// The number that we support, regardless of how many the chip has
#define MAX_N_DACS 0
#define MAX_N_RMT 0
#define MAX_N_I2SO 0
#define MAX_N_PARLIO 1
#define MAX_N_SPI 1
#define MAX_N_SDCARD 1
#define MAX_N_SIMULATOR 0

#define MAX_N_GPIO SOC_GPIO_PIN_COUNT
#define DEFAULT_STEPPING_ENGINE Stepping::PARLIO

// PARLIO produces one 16-bit word per microsecond.
#define STEPPING_FREQUENCY 1000000

const int BAUD_RATE = 115200;

#define LAST_ERROR lastError

#include <esp_task_wdt.h>
#include <esp_log.h>

#include "esp32-hal.h"

#include <esp_idf_version.h>

#include "Logging.h"

#include "wdt.h"

inline void platform_preinit() {
    configure_task_watchdog();

    // The loop task must not be subscribed to the TWDT.  FluidNC's watchdog
    // design is opt-in; blocking network bring-up is expected.
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_err_t err = esp_task_wdt_delete(NULL);
        if (err != ESP_OK) {
            log_error("esp_task_wdt_delete failed: " << esp_err_to_name(err));
        }
    }
}

inline bool should_exit() {
    return false;
}

#define USE_ARDUINO_I2C_DRIVER 0
#define USE_I2C_MASTER_DRIVER 1

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
