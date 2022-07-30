// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/spi.h"

#include <SPI.h>
#include "driver/spi_common.h"

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin) {
    // Start the SPI bus with the pins defined here.  Once it has been started,
    // those pins "stick" and subsequent attempts to restart it with defaults
    // for the miso, mosi, and sck pins are ignored
    SPI.begin(sck_pin, miso_pin, mosi_pin);  // CS is defined by each device

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = mosi_pin,
        .miso_io_num     = miso_pin,
        .sclk_io_num     = sck_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };

    // Depends on the chip variant
#define SPI_DMA_CHAN 1
    return !spi_bus_initialize(HSPI_HOST, &bus_cfg, 1);
}

void spi_deinit_bus() {
    spi_bus_free(HSPI_HOST);
    SPI.end();
}
