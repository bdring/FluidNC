// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Driver/spi.h"

#include "driver/spi_common.h"
#include "src/Config.h"

#include <sdkconfig.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#    define HSPI_HOST SPI2_HOST
#endif

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin, bool dma) {
    // Start the SPI bus with the pins defined here.  Once it has been started,
    // those pins "stick" and subsequent attempts to restart it with defaults
    // for the miso, mosi, and sck pins are ignored

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = mosi_pin,
        .miso_io_num     = miso_pin,
        .sclk_io_num     = sck_pin,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };

    // Depends on the chip variant
    return !spi_bus_initialize(HSPI_HOST, &bus_cfg, dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED);
}

void spi_deinit_bus() {
    esp_err_t err = spi_bus_free(HSPI_HOST);
    log_debug("deinit spi " << int(err));
}
