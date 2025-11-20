// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// C support routines for tmc_spi.cpp .  These routines must be complied
// by a C compiler instead of a C++ compiler because of a problem in the
// ESP32_S3 version of esp_addr.h .  It defines a FLAG_ATTR() macro in a
// way that causes compiler error on spi_ll.h

#include "hal/spi_ll.h"
#include <soc/rtc.h>
#include <esp_idf_version.h>

#include <sdkconfig.h>
#ifdef CONFIG_IDF_TARGET_ESP32S3
#    define HSPI_HOST SPI2_HOST
#    define SPI2 GPSPI2
#endif

spi_dev_t* hw = &SPI2;

static spi_ll_clock_val_t clk_reg_val = 0;

// Establish the SPI bus configuration needed for TMC device access
// This should be done one before every TMC read or write operation,
// to reconfigure the bus from whatever mode the SD card driver used.

// cppcheck-suppress unusedFunction
void tmc_spi_bus_setup() {
#if 0
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[bus_config->miso_io_num], PIN_FUNC_GPIO);
    gpio_set_direction(bus_config->miso_io_num, GPIO_MODE_INPUT);
    gpio_matrix_out(bus_config->miso_io_num, io_signal[host].spiq_out, false, false);
    gpio_matrix_in(bus_config->miso_io_num, io_signal[host].spiq_in, false);
#endif

    if (clk_reg_val == 0) {
        spi_ll_master_cal_clock(SPI_LL_PERIPH_CLK_FREQ, 2000000, 128, &clk_reg_val);
    }
    spi_ll_master_set_clock_by_reg(hw, &clk_reg_val);

    spi_ll_master_init(hw);
    spi_ll_master_set_mode(hw, 3);
    spi_ll_set_half_duplex(hw, false);

    spi_line_mode_t mode = { 1, 1, 1 };
    spi_ll_master_set_line_mode(hw, mode);  // Single-line transfers; not DIO or QIO
}

// Perform a full-duplex transfer from out/out_bitlen to in/in_bitlen
// If in_bitlen is 0, the input data will be ignored
void tmc_spi_transfer_data(const uint8_t* out, int out_bitlen, uint8_t* in, int in_bitlen) {
    spi_ll_set_mosi_bitlen(hw, out_bitlen);

    spi_ll_set_miso_bitlen(hw, in_bitlen);

    spi_ll_set_addr_bitlen(hw, 0);
    spi_ll_set_command_bitlen(hw, 0);

    spi_ll_write_buffer(hw, out, out_bitlen);
    spi_ll_enable_mosi(hw, 1);
    if (in_bitlen) {
        spi_ll_enable_miso(hw, 1);
    }

    spi_ll_clear_int_stat(hw);
#if ESP_IDF_VERSION_MAJOR < 5
    spi_ll_master_user_start(hw);
#endif
    while (!spi_ll_usr_is_done(hw)) {}

    spi_ll_read_buffer(hw, in, in_bitlen);  // No-op if in_bitlen is 0
}

// Do a single 5-byte (reg# + data) access to a TMC register,
// accounting for the number of TMC devices (index) daisy-chained
// before the target device.  For reads, this is the first register
// access that latches the register data into the output register.

// cppcheck-suppress unusedFunction
void tmc_spi_rw_reg(uint8_t cmd, uint32_t data, int index) {
    int before = index > 0 ? index - 1 : 0;

    const size_t packetLen   = 5;
    size_t       total_bytes = (before + 1) * packetLen;
    size_t       total_bits  = total_bytes * 8;

    uint8_t out[total_bytes];

    // The data always starts at the beginning of the buffer then
    // the trailing 0 bytes will push it through the chain to the
    // target chip.
    out[0] = cmd;
    out[1] = data >> 24;
    out[2] = data >> 16;
    out[3] = data >> 8;
    out[4] = data >> 0;
    memset(&out[5], 0, total_bytes - 5);

    tmc_spi_transfer_data(out, total_bits, NULL, 0);
}
