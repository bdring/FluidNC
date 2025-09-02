#pragma once

#define WEAK_LINK __attribute__((weak))
#define WITH_MBEDTLS

#define MAX_N_SDCARD 1
#define MAX_N_UARTS 2
#define MAX_N_I2SO 1
#define MAX_N_I2C 2
#define MAX_N_SPI 1
#define MAX_N_DACS 2

#define MAX_N_GPIO 47

// Serial baud rate
// The ESP32 boot text is 115200, so you will not see early startup
// messages from the ESP32 bootloader if you use a different baud rate,
// and some serial monitor programs that assume 115200 might not work.
const int BAUD_RATE = 115200;

#include "esp32-hal.h"  // disableCore0WDT
inline void platform_preinit() {
    disableCore0WDT();
}
