// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Pins/PinDetail.h"  // pinnum_t
#include "driver/spi_master.h"

bool spi_init_bus(pinnum_t sck_pin, pinnum_t miso_pin, pinnum_t mosi_pin, bool dma, int8_t sck_drive_strength, int8_t mosi_drive_strength);
void spi_deinit_bus();

// Returns devid or -1
spi_device_t spi_register_device(pinnum_t cs_pin);

void spi_unregister_device(spi_device_t devid);

bool spi_transfer(spi_device_t busid, uint8_t* outbuf, uint8_t* inbuf, size_t len);
