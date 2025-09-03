// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <driver/i2c.h>

#include "Driver/fluidnc_i2c.h"
#include "Logging.h"

// cppcheck-suppress unusedFunction
bool i2c_master_init(int32_t bus_number, pinnum_t sda_pin, pinnum_t scl_pin, uint32_t frequency) {
    i2c_config_t conf = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = (gpio_num_t)sda_pin,
        .scl_io_num    = (gpio_num_t)scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master        = { .clk_speed = frequency },
    };

    esp_err_t ret = i2c_param_config((i2c_port_t)bus_number, &conf);
    if (ret != ESP_OK) {
        log_error("i2c_param_config failed");
        return true;
    }
    ret = i2c_driver_install((i2c_port_t)bus_number, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        log_error("i2c_driver_install failed");
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
int32_t i2c_write(int32_t bus_number, uint8_t address, const uint8_t* data, size_t count) {
#if 0
        esp_err_t        ret = ESP_FAIL;
        i2c_cmd_handle_t cmd = NULL;

        //short implementation does not support zero size writes (example when scanning) PR in IDF?
        //ret =  i2c_master_write_to_device((i2c_port_t)bus_number, address, buff, size, timeOutMillis / portTICK_RATE_MS);

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
        ret = i2c_master_cmd_begin((i2c_port_t)bus_number, cmd, 10 / portTICK_RATE_MS);

    end:
        if (cmd != NULL) {
            i2c_cmd_link_delete_static(cmd);
        }
        return ret ? -1 : count;
#else
    auto err = i2c_master_write_to_device((i2c_port_t)bus_number, address, data, count, 10 / portTICK_RATE_MS);
    if (err == ESP_OK) {
        return count;
    } else {
        log_warn("Error writing to I2C device: " << static_cast<int32_t>(err));
        return -1;
    }
#endif
}

// cppcheck-suppress unusedFunction
int32_t i2c_read(int32_t bus_number, uint8_t address, uint8_t* data, size_t count) {
    return i2c_master_read_from_device((i2c_port_t)bus_number, address, data, count, 10 / portTICK_RATE_MS) ? -1 : count;
}
