// Copyright 2025 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Platform.h"
#if USE_ARDUINO_I2C_DRIVER
#    include <Wire.h>
#    include "Driver/fluidnc_i2c.h"

bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    TwoWire& i2c = bus_number ? Wire1 : Wire;
    i2c.begin(int(sda_pin), int(scl_pin), frequency);
    return false;
};
int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
    TwoWire& i2c = bus_number ? Wire1 : Wire;

    i2c.beginTransmission(address);
    for (size_t i = 0; i < count; ++i) {
        i2c.write(data[i]);
    }
    auto res = i2c.endTransmission();  // i2c_err_t, see header file
    return res ? -res : count;
}
int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    TwoWire& i2c    = bus_number ? Wire1 : Wire;
    size_t   actual = i2c.requestFrom((int)address, count);

    for (size_t i = 0; i < actual; ++i) {
        data[i] = i2c.read();
    }
    return actual;
}
#endif
