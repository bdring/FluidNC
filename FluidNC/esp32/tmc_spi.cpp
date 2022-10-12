// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// There is no .h file to define the interface to this code.
// It works by replacing weak methods in the TMCStepper library,
// namely TMCStepper::read() and TMCStepper::write()

// It uses low-level direct access to the SPI hardware instead of
// trying to use the ESP-IDF spi_master() driver.  The reason for this
// is because the SD Card driver, which uses the same SPI bus but
// with a different CS pin, requires DMA mode, but DMA mode does not
// work properly for TMC devices that have a 5-byte input packet length;
// The last few bytes get stuck somewhere and do not make it into the
// input buffer.  I tried to switch back and forth between DMA mode
// and non-DMA mode, but that turned out to be extraordinarly difficult
// since the mode is applied at the very top level of SPI bus driver setup.
// In order to switch modes, it was necessary to completely tear down
// the various levels of the SD-on-SPI driver, reinit everything, then
// restore the old setup in all of its levels.  That proved too difficult.
// So instead I went directly to the SPI hardware via the "spi_ll_"
// interface, which is basically direct access to the hardware registers
// wrapped up to look like function calls (implemented as inlines, so
// the compiler generates very compact code).  There are two downsides
// to this method, neither of which really matter in our situation.
// The first is that there is no locking to prevent this code from
// interfering with SD card access that is already in progress.  That
// is not a problem because TMC device access and SD Card access never
// happen simultaneously.  The second is that the code polls for completion
// without letting other tasks run.  That is not a problem because TMC
// register access was effectively a blocking operation anyway, so it
// doesn't matter whether it blocks at a low or high level of abstraction.
// The time for a register access is less than 70 us for an I2SO CS pin
// and about half that for a GPIO CS.

// This code assumes that the SPI bus has already been initialized,
// with SCK, MOSI, and MISO pins assigned, via SPIBus.cpp

#include "src/Logging.h"
#include <TMCStepper.h>  // https://github.com/teemuatlut/TMCStepper

#include "hal/spi_ll.h"

spi_dev_t* hw = SPI_LL_GET_HW(HSPI_HOST);

static spi_ll_clock_val_t clk_reg_val = 0;

// Establish the SPI bus configuration needed for TMC device access
// This should be done one before every TMC read or write operation,
// to reconfigure the bus from whatever mode the SD card driver used.
static void tmc_spi_bus_setup() {
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

    spi_ll_master_set_line_mode(hw, { 1, 1, 1 });  // Single-line transfers; not DIO or QIO
}

// Perform a full-duplex transfer from out/out_bitlen to in/in_bitlen
// If in_bitlen is 0, the input data will be ignored
static void tmc_spi_transfer_data(uint8_t* out, int out_bitlen, uint8_t* in, int in_bitlen) {
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
    spi_ll_master_user_start(hw);
    while (!spi_ll_usr_is_done(hw)) {}

    spi_ll_read_buffer(hw, in, in_bitlen);  // No-op if in_bitlen is 0
}

// Do a single 5-byte (reg# + data) access to a TMC register,
// accounting for the number of TMC devices (index) daisy-chained
// before the target device.  For reads, this is the first register
// access that latches the register data into the output register.
static void tmc_spi_rw_reg(uint8_t cmd, uint32_t data, int index) {
    int before = index > 0 ? index - 1 : 0;

    const size_t packetLen       = 5;
    size_t       dummy_out_bytes = before * packetLen;
    size_t       total_bytes     = (before + 1) * packetLen;
    size_t       total_bits      = total_bytes * 8;

    uint8_t out[total_bytes] = { 0 };

    // The data always starts at the beginning of the buffer then
    // the trailing 0 bytes will push it through the chain to the
    // target chip.
    out[0] = cmd;
    out[1] = data >> 24;
    out[2] = data >> 16;
    out[3] = data >> 8;
    out[4] = data >> 0;

    tmc_spi_transfer_data(out, total_bits, NULL, 0);
}

// Replace the library's weak definition of TMC2130Stepper::write()
// This is executed in the object context so it has access to class
// data such as the CS pin that switchCSpin() uses
void TMC2130Stepper::write(uint8_t reg, uint32_t data) {
    log_verbose("TMC reg 0x" << String(reg, 16) << " write 0x" << String(data, 16));
    tmc_spi_bus_setup();

    switchCSpin(0);
    tmc_spi_rw_reg(reg | 0x80, data, link_index);
    switchCSpin(1);
}

// Replace the library's weak definition of TMC2130Stepper::read()
uint32_t TMC2130Stepper::read(uint8_t reg) {
    tmc_spi_bus_setup();

    switchCSpin(0);
    tmc_spi_rw_reg(reg, 0, link_index);
    switchCSpin(1);

    // Now that we have done the initial read cycle, we must run another
    // cycle to extract the data that was latched into the output register.
    // If the TMC chips are daisy chained, we have to clock enough bits
    // to account for the chips in the chain after the target one.  The
    // data for those "after" chips will appear at the beginning of the input
    // buffer, with the desired data for the target chip at the end.
    const size_t packetLen      = 5;
    size_t       afterChips     = link_index > 0 ? chain_length - link_index : 0;
    size_t       dummy_in_bytes = afterChips * packetLen;
    size_t       total_bytes    = (afterChips + 1) * packetLen;
    size_t       total_bits     = total_bytes * 8;

    uint8_t in[total_bytes] = { 0 };

    switchCSpin(0);
    tmc_spi_transfer_data(in, total_bits, in, total_bits);

    // The received data has the dummy bytes from the trailing chips
    // at the beginning of the buffer, with the data from the target
    // chip at the end.

    uint8_t status = in[dummy_in_bytes];

    uint32_t data = (uint32_t)in[dummy_in_bytes + 1] << 24;
    data += (uint32_t)in[dummy_in_bytes + 2] << 16;
    data += (uint32_t)in[dummy_in_bytes + 3] << 8;
    data += (uint32_t)in[dummy_in_bytes + 4];
    switchCSpin(1);

    log_verbose("TMC reg 0x" << String(reg, 16) << " read 0x" << String(data, 16) << " status 0x" << String(status, 16));

    return data;
}
