// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Platform.h"
#if MAX_N_USB_HOST

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/ringbuf.h>
#include <usb/cdc_acm_host.h>
#include <atomic>

class USBHostDriver {
public:
    void init(uint32_t baud);
    void shutdown();

    int    read();
    int    peek();
    int    available();
    int    rx_buffer_available();
    void   flushRx();
    void   flushTx();

    size_t write(uint8_t c);
    size_t write(const uint8_t* buf, size_t len);

    bool isConnected() const { return _connected.load(); }
    bool isInitialized() const { return _rx_ring != nullptr && _tx_ring != nullptr; }

private:
    static constexpr size_t RX_BUF_SIZE = 4096;
    static constexpr size_t TX_BUF_SIZE = 4096;

    RingbufHandle_t _rx_ring = nullptr;
    RingbufHandle_t _tx_ring = nullptr;

    TaskHandle_t _daemon_task_handle = nullptr;
    TaskHandle_t _class_task_handle  = nullptr;

    std::atomic<bool> _connected{false};
    std::atomic<bool> _shutdown_requested{false};
    uint32_t          _baud = 1000000;

    CdcAcmDevice* _vcp_dev = nullptr;

    int  _peek_byte = -1;
    bool _has_peek  = false;

    void clearPeekCache() { _peek_byte = -1; _has_peek = false; }

    static void daemonTask(void* arg);
    static void classTask(void* arg);

    static bool rxCallback(const uint8_t* data, size_t data_len, void* user_arg);
    static void deviceEventCallback(const cdc_acm_host_dev_event_data_t* event, void* user_arg);
    static void newDeviceCallback(usb_device_handle_t usb_dev);
};

#endif // MAX_N_USB_HOST
