#pragma once

#include <esp_attr.h>
#include <esp_compiler.h>
#include <soc/soc_caps.h>

#define IRAM IRAM_ATTR
#define INLINE inline __attribute__((always_inline))

#define PACK(__Declaration__) __Declaration__ __attribute__((__packed__))

#define WEAK_LINK __attribute__((weak))
#define WITH_MBEDTLS

#define MAX_N_UARTS SOC_UART_NUM
#define MAX_N_I2C SOC_I2C_NUM
#define MAX_N_DACS SOC_DAC_PERIPH_NUM
#define MAX_N_RMT SOC_RMT_GROUPS

// The number that we support, regardless of how many the chip has
#define MAX_N_I2SO 1
#define MAX_N_SPI 1
#define MAX_N_SDCARD 1

#define MAX_N_GPIO SOC_GPIO_PIN_COUNT /* 40 */
#define DEFAULT_STEPPING_ENGINE Stepping::RMT_ENGINE

// Serial baud rate
// The ESP32 boot text is 115200, so you will not see early startup
// messages from the ESP32 bootloader if you use a different baud rate,
// and some serial monitor programs that assume 115200 might not work.
const int BAUD_RATE = 115200;

#include <esp_task_wdt.h>

#include "esp32-hal.h"  // disableCore0WDT

#include <esp_idf_version.h>

inline void platform_preinit() {
#if ESP_IDF_VERSION_MAJOR < 5
    disableCore0WDT();
#else
    // Add current task to the watchdog
    esp_task_wdt_add(NULL);  // NULL means current task
#endif
}

#define USE_ARDUINO_I2C_DRIVER 0

#if ESP_IDF_VERSION_MAJOR >= 5
// Compatibility for older compilers versions.
#    define memory_order_seq_cst seq_cst
#endif
