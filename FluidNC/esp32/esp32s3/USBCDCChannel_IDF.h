// Copyright (c) 2023, 2025 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Channel.h"
#include "lineedit.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"

class USBCDCChannel : public Channel {
private:
    Lineedit* _lineedit;

    // Line state tracking for bootloader/reset detection
    static int _state;

    // RX buffer for data received via callbacks
    static const size_t RX_BUFFER_SIZE = 1040;
    uint8_t             _rx_buffer[RX_BUFFER_SIZE];
    size_t              _rx_head;
    size_t              _rx_tail;
    size_t              _rx_count;

    // CDC interface number
    tinyusb_cdcacm_itf_t _cdc_itf;

    // Callbacks
    static void rx_callback(int itf, cdcacm_event_t* event);
    static void line_state_callback(int itf, cdcacm_event_t* event);
    static void line_coding_callback(int itf, cdcacm_event_t* event);

    // Helper methods
    size_t rx_available() const;
    int    rx_read();
    int    rx_peek();
    void   handle_line_state(bool dtr, bool rts);

public:
    USBCDCChannel(bool addCR = false);
    ~USBCDCChannel();

    void init() override;

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int    rx_buffer_available() override;
    void   flushRx() override;
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); }
    bool   realtimeOkay(char c) override;
    bool   lineComplete(char* line, char c) override;
    Error  pollLine(char* line) override;
};

extern USBCDCChannel CDCChannel;
