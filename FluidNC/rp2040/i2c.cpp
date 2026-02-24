// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 I2C driver using Arduino Wire library

#include <Wire.h>
#include "Driver/fluidnc_i2c.h"

// FluidNC I2C interface implementation for RP2040
// Uses Arduino Wire library (compatible with Earle Philhower framework)

bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    TwoWire& i2c = bus_number ? Wire1 : Wire;
    
    // Set pins before calling begin() on RP2040
    i2c.setSDA(int(sda_pin));
    i2c.setSCL(int(scl_pin));
    
    // Initialize as master
    i2c.begin();
    
    // Set clock frequency
    i2c.setClock(frequency);
    
    return false;
}

int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
    TwoWire& i2c = bus_number ? Wire1 : Wire;

    i2c.beginTransmission(address);
    for (size_t i = 0; i < count; ++i) {
        i2c.write(data[i]);
    }
    auto res = i2c.endTransmission();
    return res ? -res : count;
}

int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    TwoWire& i2c    = bus_number ? Wire1 : Wire;
    size_t   actual = i2c.requestFrom((int)address, (int)count);

    for (size_t i = 0; i < actual; ++i) {
        data[i] = i2c.read();
    }
    return actual;
}

