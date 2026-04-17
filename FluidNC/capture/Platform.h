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

#ifndef MAX_N_SDCARD
#    define MAX_N_SDCARD 1
#endif
#ifndef MAX_N_UARTS
#    define MAX_N_UARTS 2
#endif
#ifndef MAX_N_I2SO
#    define MAX_N_I2SO 0
#endif
#ifndef MAX_N_I2C
#    define MAX_N_I2C 0
#endif
#ifndef MAX_N_SPI
#    define MAX_N_SPI 1
#endif
#ifndef MAX_N_DACS
#    define MAX_N_DACS 0
#endif
#ifndef MAX_N_RMT
#    define MAX_N_RMT 0
#endif

#define DEFAULT_STEPPING_ENGINE Stepping::TIMED

const int BAUD_RATE = 115200;

inline void platform_preinit() {}
