/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <vector>
#include "usb/cdc_acm_host.h"

namespace esp_usb {
/**
 * @brief Virtual COM Port Service Class
 *
 * Virtual COM Port (VCP) service manages drivers to connected VCP devices - typically USB <-> UART converters.
 * In practice, you rarely care about specifics of the devices; you only want uniform interface for them all.
 * VCP service does just that, after you register drivers for various VCP devices, you can just call VCP::open
 * and the service will load proper driver for device that was just plugged into USB port.
 *
 * Example usage:
 * \code{.cpp}
 * VCP::register_driver<FT23x>();
 * VCP::register_driver<CP210x>();
 * VCP::register_driver<CH34x>();
 * auto vcp = VCP::open(&dev_config);
 * \endcode
 *
 * The example code assumes that you have USB Host Lib already installed.
 */
class VCP {
public:
    /**
     * @brief Register VCP driver to VCP service
     *
     * To fully leverage from VCP service functionalities, you must register VCP drivers first.
     * The driver must contain the following public members/methods;
     * #. vid: Supported VID
     * #. pids: Array of supported PIDs
     * # Constructor with (uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx) input parameters
     *
     * @tparam T VCP driver type
     */
    template<class T> static void
    register_driver(void)
    {
        static_assert(T::pids.begin() != nullptr, "Every VCP driver must contain array of supported PIDs in 'pids' array");
        static_assert(T::vid != 0, "Every VCP driver must contain supported VID in'vid' integer");
        std::vector<uint16_t> pids(T::pids.begin(), T::pids.end()); // Convert array to vector
        vcp_driver new_driver = vcp_driver([](uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx) {
            return static_cast<CdcAcmDevice *> (new T(pid, dev_config, interface_idx)); // Lambda function: Open factory method
        }, T::vid, pids);
        drivers.push_back(new_driver);
    }

    /**
     * @brief VCP factory with VID and PID
     *
     * Use this function if you know VID and PID of the device.
     * The VCP service will look for correct (already registered) driver and load it.
     *
     * @attention USB Host Library must be installed before calling this function!
     *
     * @param[in] _vid          VID of the device
     * @param[in] _pid          PID of the device
     * @param[in] dev_config    Configuration of the device
     * @param[in] interface_idx USB interface to use
     * @return std::shared_ptr<CdcAcmDevice>
     */
    static CdcAcmDevice *
    open(uint16_t _vid, uint16_t _pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx = 0);

    /**
     * @brief VCP factory
     *
     * Use this function when you want the VCP service to open any connected VCP device.
     * The VCP service will look for correct (already registered) driver and load it.
     *
     * This function will block until a valid VCP device is found or
     * until dev_config->connection_timeout_ms expires. Set timeout to 0 to wait forever.
     *
     * @note If there are more USB devices connected, the VCP service will return first successfully opened device
     * @attention USB Host Library must be installed before calling this function!
     *
     * @param[in] dev_config    Configuration of the device
     * @param[in] interface_idx USB interface to use
     * @return std::shared_ptr<CdcAcmDevice>
     */
    static CdcAcmDevice *
    open(const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx = 0);

private:
    // Default operators
    VCP() = delete; // This driver acts as a service, you can't instantiate it
    VCP(const VCP &) = delete;
    VCP &operator=(VCP &) = delete;
    bool operator== (const VCP &param) = delete;
    bool operator!= (const VCP &param) = delete;

    /**
     * @brief VCP driver structure
     */
    typedef struct vcp_driver {
        CdcAcmDevice *(*open)(uint16_t pid, const cdc_acm_host_device_config_t *dev_config, uint8_t interface_idx); /*!< Factory method of this driver */
        uint16_t vid;                                                                                               /*!< VID this driver supports */
        std::vector<uint16_t> pids;                                                                                 /*!< List of PIDs this driver supports */
        vcp_driver(CdcAcmDevice *(*open_func)(uint16_t, const cdc_acm_host_device_config_t *, uint8_t), const uint16_t _vid, const std::vector<uint16_t> &_pids): open(open_func), vid(_vid), pids(_pids) {};
    } vcp_driver;

    /**
     * @brief List of registered VCP drivers
     */
    static std::vector<vcp_driver> drivers;
}; // VCP class
}  // namespace esp_usb
