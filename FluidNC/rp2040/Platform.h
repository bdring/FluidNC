// Copyright (c) 2024 - FluidNC RP2040 Port - Platform Definition
// Build-specific configuration for RP2040

#ifndef __FLUIDNC_RP2040_H__
#define __FLUIDNC_RP2040_H__

#include "PlatformCompat.h"  // FreeRTOS compatibility layer

// Define RP2040-specific hardware constants
#define MAX_N_UARTS 2  // RP2040 has 2 UART instances
#define MAX_N_I2C 2    // RP2040 has 2 I2C instances
#define MAX_N_SPI 2    // RP2040 has 2 SPI instances
#define MAX_N_I2SO 1
// There are more GPIOs on some chips, but the Pico board only
// brings out 0-28, with 23-25 used for on-board purposes
#define MAX_N_GPIO 29  // RP2350 GPIO 0-28 (23, 24, 25, 29 connect to wireless)
#define MAX_N_SDCARD 1

// Console UART baud rate
const int BAUD_RATE = 115200;

// Timer frequency (RP2040 timer runs at 1 MHz)
#define TIMER_FREQ 1000000UL

// Stepping engine options for RP2040
#define USE_TIMED_STEPPER 1
#define MAX_N_PIO 1
#define DEFAULT_STEPPING_ENGINE Stepping::PIO_ENGINE
// #define USE_ERROR_ISR_DEBUG  // For debugging stepper ISR timing

// Platform attribute macros
// RP2040: Stream::readBytes() is not virtual
#define ESP_OVERRIDE
// RP2040: No special IRAM requirement
#define IRAM_ATTR
#define WEAK_LINK __attribute__((weak))

// Platform initialization function (declared in rp2040.cpp)
void platform_preinit();

inline bool should_exit() {
	return false;
}

#define LAST_ERROR getLastSSLError

// Memory configuration
// RP2040 has 264KB of SRAM
#define SRAM_SIZE 264 * 1024

// Filesystem options
#define USE_LITTLEFS 1
// Flash layout: 2MB max for most Pico boards
#define FLASH_SIZE 2 * 1024 * 1024
#define FILESYSTEM_SIZE 256 * 1024  // 256KB for filesystem

#endif  // __FLUIDNC_RP2040_H__
