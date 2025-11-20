// Copyright (c) 2023, 2025 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include <sdkconfig.h>
#include <esp_idf_version.h>

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && ESP_IDF_VERSION_MAJOR >= 5

#    include "USBCDCChannel_IDF.h"

#    include "Machine/MachineConfig.h"  // config
#    include "Serial.h"                 // allChannels
#    include "esp_system.h"             // esp_restart
#    include "esp_rom_sys.h"            // esp_rom_software_reset_system
#    include "freertos/FreeRTOS.h"
#    include "freertos/task.h"

// Static member initialization
int USBCDCChannel::_state = 0;

// Global instance - will be defined elsewhere if multiple instances are needed
static USBCDCChannel* g_cdc_instance = nullptr;

USBCDCChannel::USBCDCChannel(bool addCR) : Channel("usbcdc", addCR), _rx_head(0), _rx_tail(0), _rx_count(0), _cdc_itf(TINYUSB_CDC_ACM_0) {
    _lineedit      = new Lineedit(this, _line, Channel::maxLine - 1);
    g_cdc_instance = this;
}

USBCDCChannel::~USBCDCChannel() {
    if (_lineedit) {
        delete _lineedit;
        _lineedit = nullptr;
    }
    g_cdc_instance = nullptr;
}

void USBCDCChannel::rx_callback(int itf, cdcacm_event_t* event) {
    if (g_cdc_instance == nullptr) {
        return;
    }

    // Read data from TinyUSB into our RX buffer
    uint8_t buf[64];
    size_t  rx_size = 0;

    esp_err_t ret = tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, buf, sizeof(buf), &rx_size);
    if (ret == ESP_OK && rx_size > 0) {
        for (size_t i = 0; i < rx_size; i++) {
            if (g_cdc_instance->_rx_count < RX_BUFFER_SIZE) {
                g_cdc_instance->_rx_buffer[g_cdc_instance->_rx_head] = buf[i];
                g_cdc_instance->_rx_head                             = (g_cdc_instance->_rx_head + 1) % RX_BUFFER_SIZE;
                g_cdc_instance->_rx_count++;
            }
            // else: buffer overflow, drop data
        }
    }
}

void USBCDCChannel::line_state_callback(int itf, cdcacm_event_t* event) {
    if (g_cdc_instance == nullptr) {
        return;
    }

    bool dtr = event->line_state_changed_data.dtr;
    bool rts = event->line_state_changed_data.rts;

    g_cdc_instance->handle_line_state(dtr, rts);
}

void USBCDCChannel::line_coding_callback(int itf, cdcacm_event_t* event) {
// Optional: handle line coding changes
// The Arduino version prints this information in debug mode
#    if 0
    const cdc_line_coding_t* lc = event->line_coding_changed_data.p_line_coding;
    ::printf("CDC LINE CODING: bit_rate: %u, data_bits: %u, stop_bits: %u, parity: %u\n",
             lc->bit_rate, lc->data_bits, lc->stop_bits, lc->parity);
#    endif
}

void USBCDCChannel::handle_line_state(bool dtr, bool rts) {
    // Track DTR/RTS state changes for bootloader entry detection
    // 0=!r!d  1=!rd  2=r!d  3=rd
    _state <<= 4;
    _state |= ((!!rts) << 1) + (!!dtr);
    _state &= 0xfff;

    // A sequence of transitions from R1D1 to R0D0 to R1D0 triggers a persist restart
    if (_state == 0x302) {
        // Persistent restart - maintains USB connection
        esp_restart();
    }
    // A transition from R1D0 to R0D1 reboots to download/bootloader mode
    else if ((_state & 0xff) == 0x21) {
        // ESP-IDF equivalent of bootloader restart
        // This triggers the ROM bootloader
        esp_rom_software_reset_system();
    }
}

void USBCDCChannel::init() {
    // Configure TinyUSB
    const tinyusb_config_t tusb_cfg = {
        .port = TINYUSB_PORT_FULL_SPEED_0,
        .phy = {
            .skip_setup = false,
            .self_powered = false,
            .vbus_monitor_io = -1,
        },
        .task = {
            .size = 4096,
            .priority = 5,
            .xCoreID = tskNO_AFFINITY,
        },
        .descriptor = {}, // Use default descriptors
        .event_cb = nullptr,
        .event_arg = nullptr,
    };

    // Install TinyUSB driver
    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ::printf("Failed to install TinyUSB driver: %d\n", ret);
        return;
    }

    // Configure CDC ACM
    const tinyusb_config_cdcacm_t cdc_cfg = {
        .cdc_port                     = _cdc_itf,
        .callback_rx                  = &USBCDCChannel::rx_callback,
        .callback_rx_wanted_char      = nullptr,
        .callback_line_state_changed  = &USBCDCChannel::line_state_callback,
        .callback_line_coding_changed = &USBCDCChannel::line_coding_callback,
    };

    ret = tinyusb_cdcacm_init(&cdc_cfg);
    if (ret != ESP_OK) {
        ::printf("Failed to initialize CDC ACM: %d\n", ret);
        return;
    }

    // Give time for USB to enumerate
    vTaskDelay(pdMS_TO_TICKS(300));

    allChannels.registration(this);
}

size_t USBCDCChannel::rx_available() const {
    return _rx_count;
}

int USBCDCChannel::rx_read() {
    if (_rx_count == 0) {
        return -1;
    }

    uint8_t c = _rx_buffer[_rx_tail];
    _rx_tail  = (_rx_tail + 1) % RX_BUFFER_SIZE;
    _rx_count--;

    return c;
}

int USBCDCChannel::rx_peek() {
    if (_rx_count == 0) {
        return -1;
    }

    return _rx_buffer[_rx_tail];
}

size_t USBCDCChannel::write(uint8_t c) {
    size_t written = tinyusb_cdcacm_write_queue_char(_cdc_itf, (char)c);
    if (written > 0) {
        tinyusb_cdcacm_write_flush(_cdc_itf, 0);  // Non-blocking flush
    }
    return written;
}

size_t USBCDCChannel::write(const uint8_t* buffer, size_t length) {
    if (length == 0) {
        return 0;
    }

    // Replace \n with \r\n if needed
    if (_addCR) {
        size_t rem           = length;
        char   lastchar      = '\0';
        size_t j             = 0;
        size_t total_written = 0;

        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
            size_t    k = 0;

            // bufsize-1 in case the last character is \n
            while (rem && k < (bufsize - 1)) {
                char c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }

            size_t written = tinyusb_cdcacm_write_queue(_cdc_itf, modbuf, k);
            total_written += written;
            if (written < k) {
                break;  // Buffer full
            }
        }

        tinyusb_cdcacm_write_flush(_cdc_itf, 0);
        return length;  // Return original length for compatibility
    }

    size_t written = tinyusb_cdcacm_write_queue(_cdc_itf, buffer, length);
    tinyusb_cdcacm_write_flush(_cdc_itf, 0);
    return written;
}

int USBCDCChannel::available() {
    return rx_available();
}

int USBCDCChannel::peek() {
    return rx_peek();
}

int USBCDCChannel::rx_buffer_available() {
    return RX_BUFFER_SIZE - _rx_count;
}

bool USBCDCChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool USBCDCChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Error USBCDCChannel::pollLine(char* line) {
    if (_lineedit == nullptr) {
        return Error::NoData;
    }
    return Channel::pollLine(line);
}

int USBCDCChannel::read() {
    return rx_read();
}

void USBCDCChannel::flushRx() {
    _rx_head  = 0;
    _rx_tail  = 0;
    _rx_count = 0;
    Channel::flushRx();
}

size_t USBCDCChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    // First, drain anything from the queue
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
        --remlen;
    }

    if (remlen < length) {
        return length - remlen;
    }

    // Now wait for data with timeout
    TickType_t start = xTaskGetTickCount();
    while (timeout > 0) {
        if (rx_available()) {
            size_t to_read = (remlen < rx_available()) ? remlen : rx_available();
            for (size_t i = 0; i < to_read; i++) {
                int c = rx_read();
                if (c >= 0) {
                    buffer[i] = (char)c;
                } else {
                    return length - remlen + i;
                }
            }
            return length - remlen + to_read;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout) {
            break;
        }
        timeout = timeout > elapsed ? timeout - elapsed : 0;
    }

    return length - remlen;
}

#else

// When USB CDC is not enabled, provide a null implementation
#    include "../esp32/USBCDCChannel.h"  // NullChannel fallback

#endif  // CONFIG_TINYUSB_CDC_ENABLED
