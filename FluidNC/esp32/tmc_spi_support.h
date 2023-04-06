// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tmc_spi_bus_setup();
void tmc_spi_transfer_data(uint8_t* out, int out_bitlen, uint8_t* in, int in_bitlen);
void tmc_spi_rw_reg(uint8_t cmd, uint32_t data, int index);

#ifdef __cplusplus
}
#endif
