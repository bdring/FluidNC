#pragma once

#if defined(__GNUC__) || defined(__clang__)
#    define WEAK_LINK __attribute__((weak))
#else
#    define WEAK_LINK
#endif
#define IRAM_ATTR

#ifndef likely
#    define likely(x) (x)
#endif
#ifndef unlikely
#    define unlikely(x) (x)
#endif

// #define MAX_N_GPIO 40  // Same as ESP32
#define MAX_N_GPIO 49  // Same as ESP32-S3

// #define WITH_MBEDTLS

#define IRAM
#define INLINE __forceinline

#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))

#define MAX_N_SDCARD 1
#define MAX_N_UARTS 2
#define MAX_N_I2SO 0
#define MAX_N_I2C 0
#define MAX_N_SPI 1
#define MAX_N_DACS 0
#define MAX_N_RMT 0
#define MAX_N_SIMULATOR 0

#define DEFAULT_STEPPING_ENGINE Stepping::TIMED
#define STEPPING_FREQUENCY 1000000

const int BAUD_RATE = 115200;

inline void platform_preinit() {}

bool should_exit();
