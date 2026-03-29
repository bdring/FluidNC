/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdexcept>
#include "usb/vcp.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "VCP service";

namespace esp_usb {
std::vector<VCP::vcp_driver> VCP::drivers;
CdcAcmDevice *VCP::open(uint16_t _vid, uint16_t _pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx)
{
    // In case user didn't install CDC-ACM driver, we try to install it here.
    const esp_err_t err = cdc_acm_host_install(NULL);
    switch (err) {
    case ESP_OK: ESP_LOGD(TAG, "CDC-ACM driver installed"); break;
    case ESP_ERR_INVALID_STATE: ESP_LOGD(TAG, "CDC-ACM driver already installed"); break;
    default: ESP_LOGE(TAG, "Failed to install CDC-ACM driver"); return nullptr;
    }

    for (vcp_driver drv : drivers) {
        if (drv.vid == _vid) {
            for (uint16_t p : drv.pids) {
                if (p == _pid) {
                    try {
                        return drv.open(_pid, dev_config, interface_idx);
                    } catch (esp_err_t &e) {
                        switch (e) {
                        case ESP_ERR_NO_MEM: throw std::bad_alloc();
                        case ESP_ERR_NOT_FOUND: // fallthrough
                        default: return nullptr;
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

CdcAcmDevice *VCP::open(const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx)
{
    // Setup this function timeout
    TickType_t timeout_ticks = (dev_config->connection_timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(dev_config->connection_timeout_ms);
    TimeOut_t connection_timeout;
    vTaskSetTimeOutState(&connection_timeout);

    // In case user didn't install CDC-ACM driver, we try to install it here.
    esp_err_t err = cdc_acm_host_install(NULL);
    switch (err) {
    case ESP_OK: ESP_LOGD(TAG, "CDC-ACM driver installed"); break;
    case ESP_ERR_INVALID_STATE: ESP_LOGD(TAG, "CDC-ACM driver already installed"); break;
    default: ESP_LOGE(TAG, "Failed to install CDC-ACM driver"); return nullptr;
    }

    // dev_config->connection_timeout_ms is normally meant for 1 device,
    // but here it is a timeout for the whole function call
    cdc_acm_host_device_config_t _config = *dev_config;
    _config.connection_timeout_ms = 1;

    // Try opening all registered devices, return on first success
    do {
        for (vcp_driver drv : drivers) {
            for (uint16_t pid : drv.pids) {
                try {
                    return drv.open(pid, &_config, interface_idx);
                } catch (esp_err_t &e) {
                    switch (e) {
                    case ESP_ERR_NOT_FOUND: break;
                    case ESP_ERR_NO_MEM: throw std::bad_alloc();
                    default: return nullptr;
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    } while (xTaskCheckForTimeOut(&connection_timeout, &timeout_ticks) == pdFALSE);
    return nullptr;
}
} // namespace esp_usb
