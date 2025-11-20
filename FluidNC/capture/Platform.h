#pragma once

#define WEAK_LINK
#define IRAM_ATTR

#ifndef likely
#    define likely(x) (x)
#endif
#ifndef unlikely
#    define unlikely(x) (x)
#endif

#define MAX_N_GPIO 40

// #define WITH_MBEDTLS

#define IRAM
#define INLINE __forceinline

#define PACK(__Declaration__) __pragma(pack(push, 1)) __Declaration__ __pragma(pack(pop))

#define MAX_N_SDCARD 0
#define MAX_N_UARTS 2
#define MAX_N_I2SO 0
#define MAX_N_I2C 0
#define MAX_N_SPI 0
#define MAX_N_DACS 0
#define MAX_N_RMT 0

#define DEFAULT_STEPPING_ENGINE Stepping::TIMED

const int BAUD_RATE = 115200;

inline void platform_preinit() {}
