#pragma once

#define WEAK_LINK
#define IRAM_ATTR

#define MAX_N_GPIO 40

// #define WITH_MBEDTLS

#define MAX_N_SDCARD 0
#define MAX_N_UARTS 2
#define MAX_N_I2SO 0
#define MAX_N_I2C 0
#define MAX_N_SPI 0
#define MAX_N_DACS 0

const int BAUD_RATE = 115200;

inline void platform_preinit() {}
