// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Platform.h"
#if !USE_ARDUINO_I2C_DRIVER

#    include "Driver/fluidnc_i2c.h"
#    include "Logging.h"

// USE_I2C_MASTER_DRIVER (from Platform.h) selects the new I2C master driver
// (driver/i2c_master.h, IDF 5.4+).  The legacy driver (driver/i2c.h) is
// end-of-life in IDF 6, slated for removal in IDF 7, and aborts at boot if
// anything else in the firmware uses the new one.
#    if USE_I2C_MASTER_DRIVER

#        include <driver/i2c_master.h>

static constexpr int    xfer_timeout_ms = 10;
static constexpr size_t max_devices     = 8;  // per bus

struct i2c_bus_t {
    i2c_master_bus_handle_t handle = nullptr;
    uint32_t                frequency;
    struct {
        uint8_t                 address;
        i2c_master_dev_handle_t handle;
    } devices[max_devices];
    size_t n_devices = 0;
};
static i2c_bus_t buses[MAX_N_I2C];

// The new driver addresses transfers to a device handle, so add each
// address to the bus the first time it is used.
static i2c_master_dev_handle_t i2c_device(objnum_t bus_number, uint8_t address) {
    if (bus_number >= MAX_N_I2C) {
        return nullptr;
    }
    i2c_bus_t& bus = buses[bus_number];
    if (!bus.handle) {
        return nullptr;
    }
    for (size_t i = 0; i < bus.n_devices; ++i) {
        if (bus.devices[i].address == address) {
            return bus.devices[i].handle;
        }
    }
    if (bus.n_devices == max_devices) {
        log_error("Too many I2C devices on bus " << bus_number);
        return nullptr;
    }
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address      = address;
    dev_cfg.scl_speed_hz        = bus.frequency;
    i2c_master_dev_handle_t dev;
    if (i2c_master_bus_add_device(bus.handle, &dev_cfg, &dev) != ESP_OK) {
        return nullptr;
    }
    bus.devices[bus.n_devices++] = { address, dev };
    return dev;
}

// cppcheck-suppress unusedFunction
bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    if (bus_number >= MAX_N_I2C) {
        return true;
    }
    i2c_master_bus_config_t bus_cfg      = {};
    bus_cfg.i2c_port                     = i2c_port_num_t(bus_number);
    bus_cfg.sda_io_num                   = gpio_num_t(sda_pin);
    bus_cfg.scl_io_num                   = gpio_num_t(scl_pin);
    bus_cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt            = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_bus_t& bus = buses[bus_number];
    if (i2c_new_master_bus(&bus_cfg, &bus.handle) != ESP_OK) {
        log_error("i2c_new_master_bus failed");
        bus.handle = nullptr;
        return true;
    }
    bus.frequency = frequency;
    bus.n_devices = 0;
    return false;
}

// cppcheck-suppress unusedFunction
int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
    if (count == 0) {
        // A zero-length write is an address probe, e.g. a bus scan
        if (bus_number >= MAX_N_I2C || !buses[bus_number].handle) {
            return -1;
        }
        return i2c_master_probe(buses[bus_number].handle, address, xfer_timeout_ms) == ESP_OK ? 0 : -1;
    }
    auto dev = i2c_device(bus_number, address);
    if (!dev) {
        return -1;
    }
    return i2c_master_transmit(dev, data, count, xfer_timeout_ms) == ESP_OK ? int(count) : -1;
}

// cppcheck-suppress unusedFunction
int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    auto dev = i2c_device(bus_number, address);
    if (!dev) {
        return -1;
    }
    return i2c_master_receive(dev, data, count, xfer_timeout_ms) == ESP_OK ? int(count) : -1;
}

#    else

#        include <driver/i2c.h>


// cppcheck-suppress unusedFunction
bool i2c_master_init(objnum_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    i2c_config_t conf     = {};
    conf.mode             = I2C_MODE_MASTER;
    conf.sda_io_num       = (gpio_num_t)sda_pin;
    conf.scl_io_num       = (gpio_num_t)scl_pin;
    conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = frequency;

    esp_err_t ret = i2c_param_config((i2c_port_t)bus_number, &conf);
    if (ret != ESP_OK) {
        log_error("i2c_param_config failed");
        return true;
    }
    ret = i2c_driver_install((i2c_port_t)bus_number, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        return true;
    }

    // Source: esp32-hal-i2c.c

    // Clock Stretching Timeout: 20b:esp32, 5b:esp32-c3, 24b:esp32-s2
    //
    // #ifdef CONFIG_IDF_TARGET_ESP32S3
    //     i2c_set_timeout((i2c_port_t)bus_number, 0x0000001FU);
    // #else
    //     i2c_set_timeout((i2c_port_t)bus_number, 0xfffff);
    // #endif
    return false;
}

// cppcheck-suppress unusedFunction
int i2c_write(objnum_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
#    if 0
        esp_err_t        ret = ESP_FAIL;
        i2c_cmd_handle_t cmd = NULL;

        //short implementation does not support zero size writes (example when scanning) PR in IDF?
        //ret =  i2c_master_write_to_device((i2c_port_t)bus_number, address, buff, size, timeOutMillis / portTICK_PERIOD_MS);

        uint8_t cmd_buff[I2C_LINK_RECOMMENDED_SIZE(1)] = { 0 };

        cmd = i2c_cmd_link_create_static(cmd_buff, I2C_LINK_RECOMMENDED_SIZE(1));
        ret = i2c_master_start(cmd);
        if (ret != ESP_OK) {
            goto end;
        }
        ret = i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, true);
        if (ret != ESP_OK) {
            goto end;
        }
        if (count) {
            ret = i2c_master_write(cmd, data, count, true);
            if (ret != ESP_OK) {
                goto end;
            }
        }
        ret = i2c_master_stop(cmd);
        if (ret != ESP_OK) {
            goto end;
        }
        ret = i2c_master_cmd_begin((i2c_port_t)bus_number, cmd, 10 / portTICK_PERIOD_MS);

    end:
        if (cmd != NULL) {
            i2c_cmd_link_delete_static(cmd);
        }
        return ret ? -1 : count;
#    else
    auto err = i2c_master_write_to_device((i2c_port_t)bus_number, address, data, count, 10 / portTICK_PERIOD_MS);
    if (err == ESP_OK) {
        return count;
    } else {
        return -1;
    }
#    endif
}

// cppcheck-suppress unusedFunction
int i2c_read(objnum_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    return i2c_master_read_from_device((i2c_port_t)bus_number, address, data, count, 10 / portTICK_PERIOD_MS) ? -1 : count;
}
#    endif
#endif
