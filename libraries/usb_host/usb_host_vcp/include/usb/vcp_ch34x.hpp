/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2021 WCH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <memory>
#include <vector>
#include "usb/cdc_acm_host.h"

#define NANJING_QINHENG_MICROE_VID (0x1A86)
#define CH340_PID                  (0x7522)
#define CH340_PID_1                (0x7523)
#define CH341_PID                  (0x5523)

namespace esp_usb {
class CH34x : public CdcAcmDevice {
public:
    /**
     * @brief Constructor for this CH34x driver
     *
     * @note USB Host library and CDC-ACM driver must be already installed
     *
     * @param[in] pid            PID eg. CH340_PID
     * @param[in] dev_config     CDC device configuration
     * @param[in] interface_idx  Interface number
     * @return CdcAcmDevice      Pointer to created and opened CH34x device
     */
    CH34x(uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx = 0);

    /**
     * @brief Set Line Coding method
     *
     * @note Overrides default implementation in CDC-ACM driver
     * @param[in] line_coding Line Coding structure
     * @return esp_err_t
     */
    esp_err_t line_coding_set(cdc_acm_line_coding_t *line_coding);

    /**
     * @brief Set Control Line State method
     *
     * @note Overrides default implementation in CDC-ACM driver
     * @note Both signals are active low
     * @param[in] dtr Indicates to DCE if DTE is present or not. This signal corresponds to V.24 signal 108/2 and RS-232 signal Data Terminal Ready.
     * @param[in] rts Carrier control for half duplex modems. This signal corresponds to V.24 signal 105 and RS-232 signal Request To Send.
     * @return esp_err_t
     */
    esp_err_t set_control_line_state(bool dtr, bool rts);

    // List of supported VIDs and PIDs
    static constexpr uint16_t vid = NANJING_QINHENG_MICROE_VID;
    static constexpr std::array<uint16_t, 3> pids = {CH340_PID, CH340_PID_1, CH341_PID};

private:
    const uint8_t intf;

    // Make open functions from CdcAcmDevice class private
    using CdcAcmDevice::open;
    using CdcAcmDevice::open_vendor_specific;
    using CdcAcmDevice::send_break; // Break is not supported by CH34x
    using CdcAcmDevice::line_coding_get; // Manufacturer doesn't provide enough information to implement this

    // This function comes from official Linux driver
    static int calculate_baud_divisor(unsigned int baud_rate, unsigned char *factor, unsigned char *divisor);
};
} // namespace esp_usb
