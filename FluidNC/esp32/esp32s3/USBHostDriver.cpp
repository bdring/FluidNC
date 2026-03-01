// Copyright (c) 2026 - Algy Tynan
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef USB_HOST_ENABLED

#include "USBHostDriver.h"

#include <usb/vcp.hpp>
#include <usb/vcp_ch34x.hpp>
#include <usb/vcp_cp210x.hpp>
#include <usb/vcp_ftdi.hpp>

// FluidNC logging (not ESP_LOG -- follow codebase convention)
#include "Report.h"
#include "NutsBolts.h"  // to_hex()

// ---------------------------------------------------------------
// Callbacks (static -> instance via user_arg)
// ---------------------------------------------------------------

// Called from CDC-ACM driver task when data arrives from USB device.
bool USBHostDriver::rxCallback(const uint8_t* data, size_t data_len, void* user_arg) {
    auto* self = static_cast<USBHostDriver*>(user_arg);
    if (self->_rx_ring && data_len > 0) {
        // Non-blocking send -- drops data if ring is full
        xRingbufferSend(self->_rx_ring, data, data_len, 0);
    }
    return true;  // true = flush USB buffer (ready for next transfer)
}

// Called from CDC-ACM driver task on device disconnect or error.
void USBHostDriver::deviceEventCallback(const cdc_acm_host_dev_event_data_t* event, void* user_arg) {
    auto* self = static_cast<USBHostDriver*>(user_arg);
    switch (event->type) {
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            self->_connected.store(false);
            self->_vcp_dev = nullptr;
            self->flushRx();
            self->flushTx();
            log_info("USB Host: device disconnected");
            break;
        default:
            log_warn("USB Host: device event " << (int)event->type);
            break;
    }
}

// Called for any new USB device attachment (before VCP matching).
void USBHostDriver::newDeviceCallback(usb_device_handle_t usb_dev) {
    const usb_device_desc_t* desc = nullptr;
    usb_host_get_device_descriptor(usb_dev, &desc);
    if (desc) {
        log_info("USB Host: device attached (VID:" << to_hex(desc->idVendor)
                 << " PID:" << to_hex(desc->idProduct) << ")");
    } else {
        log_info("USB Host: unknown device attached");
    }
}

// ---------------------------------------------------------------
// FreeRTOS Tasks
// ---------------------------------------------------------------

// Daemon task: pumps the USB host library event loop.
// Must run continuously or USB stack stops processing.
void USBHostDriver::daemonTask(void* arg) {
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
}

// Class task: installs VCP drivers, opens device, pumps TX data.
void USBHostDriver::classTask(void* arg) {
    auto* self = static_cast<USBHostDriver*>(arg);

    // 1. Install CDC-ACM host driver
    const cdc_acm_host_driver_config_t driver_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority   = 5,
        .xCoreID                = 1,
        .new_dev_cb             = newDeviceCallback,
    };
    esp_err_t err = cdc_acm_host_install(&driver_config);
    if (err != ESP_OK) {
        log_error("USB Host: CDC-ACM driver install failed: " << esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    // 2. Register VCP drivers for auto-detection
    using namespace esp_usb;
    VCP::register_driver<CH34x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<FT23x>();

    log_info("USB Host: waiting for device...");

    // 3. Device open/reopen loop (handles hot-plug reconnect)
    while (true) {
        const cdc_acm_host_device_config_t dev_config = {
            .connection_timeout_ms = 0,     // Wait forever for device
            .out_buffer_size       = 512,
            .in_buffer_size        = 512,
            .event_cb              = USBHostDriver::deviceEventCallback,
            .data_cb               = USBHostDriver::rxCallback,
            .user_arg              = self,
        };

        // VCP::open() blocks until a supported device connects.
        // Returns a raw CdcAcmDevice* owned by the caller.
        CdcAcmDevice* dev = VCP::open(&dev_config);
        if (!dev) {
            log_warn("USB Host: VCP::open() returned null, retrying...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 4. Configure line coding (baud rate)
        cdc_acm_line_coding_t line_coding = {
            .dwDTERate   = self->_baud,
            .bCharFormat = 0,  // 1 stop bit
            .bParityType = 0,  // No parity
            .bDataBits   = 8,
        };
        err = dev->line_coding_set(&line_coding);
        if (err != ESP_OK) {
            log_warn("USB Host: line_coding_set failed: " << esp_err_to_name(err));
        }

        // Set DTR+RTS (some USB-serial chips need this to enable data flow)
        dev->set_control_line_state(true, true);

        self->_vcp_dev = dev;
        self->_connected.store(true);
        log_info("USB Host: device connected, baud " << self->_baud);

        // 5. TX pump loop: runs while device is connected
        while (self->_connected.load()) {
            size_t item_size = 0;
            uint8_t* tx_data = static_cast<uint8_t*>(
                xRingbufferReceive(self->_tx_ring, &item_size, pdMS_TO_TICKS(10))
            );
            if (tx_data && item_size > 0) {
                err = dev->tx_blocking(tx_data, item_size, 100);
                vRingbufferReturnItem(self->_tx_ring, tx_data);

                if (err == ESP_ERR_TIMEOUT) {
                    // USB device busy -- not an error
                } else if (err == ESP_ERR_INVALID_STATE) {
                    // Device disconnected mid-transfer
                    log_info("USB Host: device gone during TX");
                    break;
                } else if (err != ESP_OK) {
                    log_warn("USB Host: TX error: " << esp_err_to_name(err));
                }
            }
        }

        // Device disconnected -- clean up and loop back to VCP::open()
        delete dev;
        log_info("USB Host: waiting for reconnect...");
    }
}

// ---------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------

void USBHostDriver::init(uint32_t baud) {
    _baud = baud;

    // Create ring buffers (byte buffers -- no per-item overhead)
    _rx_ring = xRingbufferCreate(RX_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    _tx_ring = xRingbufferCreate(TX_BUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!_rx_ring || !_tx_ring) {
        log_error("USB Host: ring buffer creation failed");
        return;
    }

    // Install USB host library
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        log_error("USB Host: usb_host_install failed: " << esp_err_to_name(err));
        return;
    }

    // Daemon task: pumps USB host library events
    xTaskCreatePinnedToCore(daemonTask, "usb_daemon", 4096, this, 2, &_daemon_task_handle, 1);

    // Class task: installs VCP drivers, opens device, pumps TX
    xTaskCreatePinnedToCore(classTask, "usb_class", 4096, this, 3, &_class_task_handle, 1);

    log_info("USB Host: stack initialised");
}

void USBHostDriver::shutdown() {
    _connected.store(false);
    if (_daemon_task_handle) {
        vTaskDelete(_daemon_task_handle);
        _daemon_task_handle = nullptr;
    }
    if (_class_task_handle) {
        vTaskDelete(_class_task_handle);
        _class_task_handle = nullptr;
    }
    cdc_acm_host_uninstall();
    usb_host_uninstall();
    if (_rx_ring) { vRingbufferDelete(_rx_ring); _rx_ring = nullptr; }
    if (_tx_ring) { vRingbufferDelete(_tx_ring); _tx_ring = nullptr; }
}

// ---------------------------------------------------------------
// Ring Buffer Access (called from Channel on main task)
// ---------------------------------------------------------------

int USBHostDriver::read() {
    size_t item_size = 0;
    uint8_t* item = static_cast<uint8_t*>(
        xRingbufferReceive(_rx_ring, &item_size, 0)
    );
    if (!item || item_size == 0) return -1;
    uint8_t byte = item[0];
    vRingbufferReturnItem(_rx_ring, item);
    return byte;
}

int USBHostDriver::peek() {
    // Ring buffers don't support peek natively.
    // Read one byte, then push it back.
    size_t item_size = 0;
    uint8_t* item = static_cast<uint8_t*>(
        xRingbufferReceive(_rx_ring, &item_size, 0)
    );
    if (!item || item_size == 0) return -1;
    uint8_t byte = item[0];
    vRingbufferReturnItem(_rx_ring, item);
    // Push it back -- safe because we just freed the space
    xRingbufferSend(_rx_ring, &byte, 1, 0);
    return byte;
}

int USBHostDriver::available() {
    return RX_BUF_SIZE - xRingbufferGetCurFreeSize(_rx_ring);
}

int USBHostDriver::rxBufferAvailable() {
    return xRingbufferGetCurFreeSize(_rx_ring);
}

void USBHostDriver::flushRx() {
    size_t item_size;
    while (void* item = xRingbufferReceive(_rx_ring, &item_size, 0)) {
        vRingbufferReturnItem(_rx_ring, item);
    }
}

void USBHostDriver::flushTx() {
    size_t item_size;
    while (void* item = xRingbufferReceive(_tx_ring, &item_size, 0)) {
        vRingbufferReturnItem(_tx_ring, item);
    }
}

size_t USBHostDriver::write(uint8_t c) {
    return write(&c, 1);
}

size_t USBHostDriver::write(const uint8_t* buf, size_t len) {
    if (!_connected.load() || !_tx_ring) return 0;
    if (xRingbufferSend(_tx_ring, buf, len, 0) == pdTRUE) {
        return len;
    }
    return 0;  // TX ring full -- data dropped
}

#endif // USB_HOST_ENABLED
