// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#ifdef USB_HOST_ENABLED

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <usb/cdc_acm_host.h>
#include <atomic>

// Forward-declare to avoid pulling USB headers into every translation unit
namespace esp_usb {
    class CdcAcmDevice;
}

class USBHostDriver {
public:
    void init(uint32_t baud);
    void shutdown();

    // Thread-safe access (called from Channel on main task)
    int    read();               // Returns byte or -1 if empty
    int    peek();               // Returns byte without consuming, or -1
    int    available();          // Bytes available in RX ring
    int    rxBufferAvailable();  // Free space in RX ring
    void   flushRx();            // Discard all RX data
    void   flushTx();            // Discard all pending TX data

    size_t write(uint8_t c);
    size_t write(const uint8_t* buf, size_t len);

    bool isConnected() const { return _connected.load(); }

private:
    static constexpr size_t RX_BUF_SIZE = 4096;
    static constexpr size_t TX_BUF_SIZE = 4096;

    RingbufHandle_t _rx_ring = nullptr;  // device -> host (pendant -> FluidNC)
    RingbufHandle_t _tx_ring = nullptr;  // host -> device (FluidNC -> pendant)

    TaskHandle_t _daemon_task_handle = nullptr;
    TaskHandle_t _class_task_handle  = nullptr;

    std::atomic<bool> _connected{false};
    uint32_t          _baud = 1000000;

    esp_usb::CdcAcmDevice* _vcp_dev = nullptr;  // Owned by VCP service, not us

    // FreeRTOS task entry points (static -> instance via arg)
    static void daemonTask(void* arg);
    static void classTask(void* arg);

    // RX data callback -- called from CDC-ACM driver task
    static bool rxCallback(const uint8_t* data, size_t data_len, void* user_arg);

    // Device event callback -- called for disconnect/error
    static void deviceEventCallback(const cdc_acm_host_dev_event_data_t* event, void* user_arg);

    // New device callback -- logs VID:PID for any attached device
    static void newDeviceCallback(usb_device_handle_t usb_dev);
};

#endif // USB_HOST_ENABLED
