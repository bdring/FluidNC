// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Pins/PinDetail.h"  // pinnum_t

// I2C interface

bool i2c_master_init(int bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency);
int  i2c_write(int bus_number, uint8_t address, const uint8_t* data, size_t count);
int  i2c_read(int bus_number, uint8_t address, uint8_t* data, size_t count);
