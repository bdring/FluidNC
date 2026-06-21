// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <esp_log.h>
#undef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL CORE_DEBUG_LEVEL

#include <vfs_api.h>
#include <esp_vfs_fat.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <ff.h>
#include <soc/sdmmc_struct.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <esp_error.hpp>

#include "Driver/sdspi.h"
#include "Config.h"

#define CHECK_EXECUTE_RESULT(err, str)                                                                                                     \
    do {                                                                                                                                   \
        if ((err) != ESP_OK) {                                                                                                             \
            log_error(str << " code " << to_hex(err));                                                                                     \
            goto cleanup;                                                                                                                  \
        }                                                                                                                                  \
    } while (0)

static esp_err_t mount_to_vfs_fat(uint32_t max_files, sdmmc_card_t* card, uint8_t pdrv, const char* base_path) {
    FATFS*    fs = NULL;
    esp_err_t err;
    ff_diskio_register_sdmmc(pdrv, card);

    //    ESP_LOGD(TAG, "using pdrv=%i", pdrv);
    // Drive names are "0:", "1:", etc.
    const char drv[3] = { (char)('0' + pdrv), ':', 0 };

    FRESULT res;

    // connect FATFS to VFS
    err = esp_vfs_fat_register(base_path, drv, max_files, &fs);
    if (err == ESP_ERR_INVALID_STATE) {
        // it's okay, already registered with VFS
    } else if (err != ESP_OK) {
        //        ESP_LOGD(TAG, "esp_vfs_fat_register failed 0x(%x)", err);
        goto fail;
    }

    // Try to mount partition
    res = f_mount(fs, drv, 1);
    if (res != FR_OK) {
        err = ESP_FAIL;
        //        ESP_LOGW(TAG, "failed to mount card (%d)", res);
        goto fail;
    }
    return ESP_OK;

fail:
    if (fs) {
        f_mount(NULL, drv, 0);
    }
    esp_vfs_fat_unregister_path(base_path);
    ff_diskio_unregister(pdrv);
    return err;
}

// NOTE: SDSPI_HOST_DEFAULT is incomplete and will give a warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
sdmmc_host_t  host_config = SDSPI_HOST_DEFAULT();
#pragma GCC diagnostic pop

sdmmc_card_t* card        = NULL;
const char*   base_path   = "/sd";

static void call_host_deinit(const sdmmc_host_t* host_config) {
    if (host_config->flags & SDMMC_HOST_FLAG_DEINIT_ARG) {
        host_config->deinit_p(host_config->slot);
    } else {
        host_config->deinit();
    }
}

// cppcheck-suppress unusedFunction
bool sd_init_slot(uint32_t freq_hz, pinnum_t cs_pin, pinnum_t cd_pin, pinnum_t wp_pin) {
    esp_err_t err;

    esp_log_level_set("sdmmc_sd", ESP_LOG_NONE);
    esp_log_level_set("sdmmc_common", ESP_LOG_NONE);

    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.

    bool host_inited = false;

    sdspi_device_config_t slot_config;

    host_config.flags &= ~SDMMC_HOST_FLAG_DDR;
    host_config.max_freq_khz = freq_hz / 1000;

    err = host_config.init();
    CHECK_EXECUTE_RESULT(err, "host init failed");
    host_inited = true;

    // Attach a set of GPIOs to the SPI SD card slot
    slot_config         = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = static_cast<spi_host_device_t>(host_config.slot);
    slot_config.gpio_cs = gpio_num_t(cs_pin);
    slot_config.gpio_cd = gpio_num_t(cd_pin);
    slot_config.gpio_wp = gpio_num_t(wp_pin);

    err = sdspi_host_init_device(&slot_config, &(host_config.slot));
    CHECK_EXECUTE_RESULT(err, "slot init failed");

    SDMMC.clock.phase_dout = 1;
    SDMMC.clock.phase_din  = 6;

    return true;

cleanup:
    if (host_inited) {
        call_host_deinit(&host_config);
    }
    return false;
}

// adapted from vfs_fat_sdmmc.c:esp_vfs_fat_sdmmc_mount()
// cppcheck-suppress unusedFunction
std::error_code sd_mount(uint32_t max_files) {
    log_verbose("Mount_sd");
    esp_err_t err;

    if ((err = host_config.set_card_clk(host_config.slot, host_config.max_freq_khz)) != ESP_OK) {
        log_debug("spi_set_card_clk failed");
        return esp_error::make_error_code(err);
    }

    // mount_prepare_mem() ... minus the strdup of base_path
    // Search for a free drive slot
    BYTE pdrv = FF_DRV_NOT_USED;
    if ((err = ff_diskio_get_drive(&pdrv)) != ESP_OK) {
        log_debug("ff_diskio_get_drive failed");
        return esp_error::make_error_code(err);
    }
    if (pdrv == FF_DRV_NOT_USED) {
        log_debug("the maximum count of volumes is already mounted");
        return esp_error::make_error_code(ESP_FAIL);
    }
    // pdrv is now the index of the unused drive slot

    // not using ff_memalloc here, as allocation in internal RAM is preferred
    card = static_cast<sdmmc_card_t*>(malloc(sizeof(sdmmc_card_t)));
    if (card == NULL) {
        log_debug("could not allocate new sdmmc_card_t");
        return esp_error::make_error_code(ESP_ERR_NO_MEM);
    }
    // /mount_prepare_mem()

    // probe and initialize card
    err = sdmmc_card_init(&host_config, card);
    if (err != ESP_OK) {
        // Some cards fail the first time after they are inserted, but then succeed,
        // so we retry this step once.
        err = sdmmc_card_init(&host_config, card);
    }
    CHECK_EXECUTE_RESULT(err, "sdmmc_card_init failed");

    err = mount_to_vfs_fat(max_files, card, pdrv, base_path);
    CHECK_EXECUTE_RESULT(err, "mount_to_vfs failed");

    return {};
cleanup:
    free(card);
    card = NULL;
    return esp_error::make_error_code(err);
}

// cppcheck-suppress unusedFunction
void sd_unmount() {
    BYTE pdrv = ff_diskio_get_pdrv_card(card);
    if (pdrv == 0xff) {
        return;
    }

    // unmount
    const char drv[3] = { (char)('0' + pdrv), ':', 0 };
    f_mount(NULL, drv, 0);

    esp_vfs_fat_unregister_path(base_path);

    // release SD driver
    ff_diskio_unregister(pdrv);

    free(card);
    card = NULL;
}

// cppcheck-suppress unusedFunction
void sd_deinit_slot() {
    sdspi_host_remove_device(host_config.slot);
    call_host_deinit(&host_config);

    //deinitialize the bus after all devices are removed
    //    spi_bus_free(HSPI_HOST);
}
