// Platform compatibility header for FluidNC
// Handles differences between ESP32 and RP2040

#pragma once

#include <stddef.h>    // size_t
#include <cstdint>     // uint64_t, etc.

// ============ Platform Detection ============
#ifdef CONFIG_IDF_VERSION
    // ESP32 (IDF provides CONFIG_IDF_VERSION macro)
    #define FLUIDNC_PLATFORM_ESP32 1
    #define FLUIDNC_PLATFORM_RP2040 0
#else
    // RP2040 or other platform
    #define FLUIDNC_PLATFORM_ESP32 0
    #define FLUIDNC_PLATFORM_RP2040 1
#endif

// ============ FreeRTOS Compatibility ============
// Both ESP32 and RP2040 (with Earle Philhower framework) have FreeRTOS
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#define HAS_FREERTOS 1

// Platform-specific adjustments
#if FLUIDNC_PLATFORM_ESP32
    // ESP32 specific configuration
#else
    // RP2040 specific configuration (if needed)
#endif

// ============ WiFi/Network Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #define HAS_WIFI 1
    #define HAS_BLUETOOTH 1
    #include <esp_wifi_types.h>
#else
    // RP2040: WiFi only on Pico W (CYW43), need special build
    #ifdef PICO_W
        #define HAS_WIFI 1
        #define HAS_BLUETOOTH 0
    #else
        #define HAS_WIFI 0
        #define HAS_BLUETOOTH 0
    #endif
#endif
#if 0
// ============ OTA Update Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #include <esp_ota_ops.h>
    #define HAS_OTA 1
#else
    // RP2040: OTA not yet implemented
    #define HAS_OTA 0
    // Provide stub types/functions
    typedef void* esp_ota_handle_t;
    typedef void* const esp_partition_t;
    extern void* esp_ota_get_next_update_partition(const void* start_from);
    extern int esp_ota_begin(const void* partition, size_t image_size, void* out_handle);
    extern int esp_ota_write(void* handle, const void* data, size_t size);
    extern int esp_ota_end(void* handle);
#endif
#endif

// ============ Bluetooth Compatibility ============
#if FLUIDNC_PLATFORM_ESP32 && HAS_BLUETOOTH
    #define HAS_BT_CLASSIC 1
    #define HAS_BT_BLE 1
#else
    #define HAS_BT_CLASSIC 0
    #define HAS_BT_BLE 0
#endif

// ============ Watchdog Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #include <esp_task_wdt.h>
    #define feed_watchdog() esp_task_wdt_reset()
#else
    // RP2040: Implemented in rp2040/wdt.cpp
    extern void feed_watchdog();
#endif

// ============ Timer/Timing Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #include <sys/time.h>
    typedef uint64_t micros_t;
#else
    // RP2040: Use Pico SDK timers
    typedef uint64_t micros_t;
#endif

// ============ Memory Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #if CONFIG_SPIRAM
        #define HAS_EXTERNAL_RAM 1
    #else
        #define HAS_EXTERNAL_RAM 0
    #endif
    #define TASK_STACK_SIZE 4096
#else
    // RP2040: Limited internal RAM (264KB total)
    #define HAS_EXTERNAL_RAM 0
    #define TASK_STACK_SIZE 1024
#endif

// ============ Debug/Console Compatibility ============
#if FLUIDNC_PLATFORM_ESP32
    #include <esp_log.h>
    #define DEBUG_LOG(fmt, ...) ESP_LOGI("FNC", fmt, ##__VA_ARGS__)
#else
    // RP2040: Use stdio/printf
    #include <stdio.h>
    #define DEBUG_LOG(fmt, ...) printf("FNC: " fmt "\n", ##__VA_ARGS__)
    
    // Undefine problematic Arduino macros that conflict with std::chrono and other STL components  
    // Mbed's Arduino.h defines abs(), min(), max() as single-argument macros
    #undef abs
    #undef min  
    #undef max
#endif