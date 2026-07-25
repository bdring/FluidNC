// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 SD card support using Arduino SD library
// Provides SPI-based SD card mounting and file system access via FATFS

#include "Driver/sdspi.h"
#include "Driver/fluidnc_gpio.h"
#include "Logging.h"
#include "VFS.h"
#include <SD.h>
#include <system_error>

// Global SD card state
static bool sd_mounted = false;
const char* base_path  = "/sd";

// SD card configuration
static uint32_t _freq_hz = 4000000;  // Default 4 MHz SPI speed
static pinnum_t _cs_pin  = -1;
static pinnum_t _cd_pin  = -1;
static pinnum_t _wp_pin  = -1;

// Initialize the SD card slot with specified pins and frequency
// cppcheck-suppress unusedFunction
bool sd_init_slot(uint32_t freq_hz, pinnum_t cs_pin, pinnum_t cd_pin, pinnum_t wp_pin) {
    log_info("Initializing SD card slot: freq=" << freq_hz << " Hz, CS=" << (int)cs_pin);

    _freq_hz = freq_hz;
    _cs_pin  = cs_pin;
    _cd_pin  = cd_pin;
    _wp_pin  = wp_pin;

    // For RP2040, the Arduino SD library will handle the SPI initialization
    // We just need to set the CS pin. The other pins (MOSI/MISO/CLK) are typically
    // hardwired to the SPI interface.

    // Note: The actual SPI initialization happens in sd_mount() when SD.begin() is called
    // We just verify that the CS pin is valid here.
    if (_cs_pin == -1) {
        log_error("Invalid CS pin for SD card");
        return false;
    }

    log_info("SD card slot initialized");
    return true;
}

// Mount the SD card to the file system
// cppcheck-suppress unusedFunction
std::error_code sd_mount(uint32_t max_files) {
    if (sd_mounted) {
        log_debug("SD card already mounted");
        return {};
    }

    if (_cs_pin == -1) {
        log_error("SD card slot not initialized");
        return std::make_error_code(std::errc::no_such_device);
    }

    log_info("Mounting SD card on CS pin " << (int)_cs_pin);

    // Initialize the SD card using Arduino SD library
    // Arduino's SD.begin() takes the CS pin and optional SPI speed
    if (!SD.begin(_cs_pin)) {
        log_error("SD card mount failed - card initialization error");
        return std::make_error_code(std::errc::io_error);
    }
    SD.setTimeCallback(nullptr);

    sd_mounted = true;
    log_info("SD card mounted successfully at " << base_path);

    VFS.map("/sd", SDFS);

    return {};
}

// Unmount the SD card
// cppcheck-suppress unusedFunction
void sd_unmount() {
    if (!sd_mounted) {
        return;
    }
    
    // Arduino SD library doesn't have an explicit unmount, but we can
    // close the connection by reinitializing or stopping the SPI
    // For now, we just mark as unmounted and log it
    log_info("SD card unmounted");
    sd_mounted = false;
}

// Deinitialize the SD card slot
// cppcheck-suppress unusedFunction
void sd_deinit_slot() {
    if (sd_mounted) {
        sd_unmount();
    }
    
    _cs_pin = -1;
    _freq_hz = 4000000;
    log_info("SD card slot deinitialized");
}
