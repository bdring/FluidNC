// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 SPI driver using pico-sdk

#include "Driver/spi.h"
#include "Driver/fluidnc_gpio.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// RP2040 has two SPI instances: spi0 and spi1
// SPI0 default pins: CLK=GPIO18, MOSI=GPIO19, MISO=GPIO16
// SPI1 default pins: CLK=GPIO10, MOSI=GPIO11, MISO=GPIO12

// Track which SPI instance is initialized
static spi_inst_t* _active_spi = nullptr;
static bool _spi_initialized = false;

// Default baudrate for SPI bus (can be overridden in machine config)
static const uint32_t SPI_DEFAULT_BAUDRATE = 1000000;  // 1 MHz

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin, bool dma, int8_t sck_drive_strength, int8_t mosi_drive_strength) {
    // For RP2040, we need to select which SPI instance to use based on pin configuration
    // The pin configuration must match one of the valid pin combinations
    
    spi_inst_t* spi = nullptr;
    
    // Map pins to SPI instance
    // SPI0: CLK on GP18, MOSI on GP19, MISO on GP16
    if (sck_pin == 18 && mosi_pin == 19 && miso_pin == 16) {
        spi = spi0;
    }
    // SPI1: CLK on GP10, MOSI on GP11, MISO on GP12
    else if (sck_pin == 10 && mosi_pin == 11 && miso_pin == 12) {
        spi = spi1;
    }
    // Alternative SPI0 pins: CLK on GP2, MOSI on GP3, MISO on GP4
    else if (sck_pin == 2 && mosi_pin == 3 && miso_pin == 4) {
        spi = spi0;
    }
    // Alternative SPI1 pins: CLK on GP14, MOSI on GP15, MISO on GP8
    else if (sck_pin == 14 && mosi_pin == 15 && miso_pin == 8) {
        spi = spi1;
    }
    else {
        // Invalid pin combination
        return false;
    }
    
    if (_spi_initialized) {
        // SPI bus already initialized
        return true;
    }
    
    // Initialize SPI at default baudrate
    uint actual_baudrate = spi_init(spi, SPI_DEFAULT_BAUDRATE);
    
    // Configure pin functions for SPI
    gpio_set_function(sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(miso_pin, GPIO_FUNC_SPI);
    
    // Set pull-ups on MOSI and MISO (SCK typically doesn't need pull)
    gpio_pull_up(mosi_pin);
    gpio_pull_up(miso_pin);
    
    // Apply drive strength if specified
    if (sck_drive_strength != -1) {
        gpio_drive_strength(sck_pin, sck_drive_strength);
    }
    if (mosi_drive_strength != -1) {
        gpio_drive_strength(mosi_pin, mosi_drive_strength);
    }
    
    // Configure SPI format: 8-bit, SPI mode 0 (CPOL=0, CPHA=0)
    spi_set_format(spi,
                   8,                    // data bits
                   SPI_CPOL_0,           // clock polarity
                   SPI_CPHA_0,           // clock phase
                   SPI_MSB_FIRST);       // bit order
    
    // Note: DMA parameter is currently ignored for RP2040 implementation
    // SPI transfers can be enhanced with DMA in future iterations
    
    _active_spi = spi;
    _spi_initialized = true;
    
    return true;
}

void spi_deinit_bus() {
    if (_spi_initialized && _active_spi != nullptr) {
        spi_deinit(_active_spi);
        _spi_initialized = false;
        _active_spi = nullptr;
    }
}
