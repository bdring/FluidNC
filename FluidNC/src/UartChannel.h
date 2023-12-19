// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Uart.h"
#include "Channel.h"
#include "lineedit.h"

class UartChannel : public Channel, public Configuration::Configurable {
private:
    Lineedit* _lineedit;
    Uart*     _uart;

    int _uart_num           = 0;
    int _report_interval_ms = 0;

public:
    UartChannel(bool addCR = false);

    void init();
    void init(Uart* uart);

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int      rx_buffer_available() override;
    void     flushRx() override;
    size_t   timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t   timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    bool     realtimeOkay(char c) override;
    bool     lineComplete(char* line, char c) override;
    Channel* pollLine(char* line) override;

    // Configuration methods
    void group(Configuration::HandlerBase& handler) override {
        handler.item("uart_num", _uart_num);
        handler.item("report_interval_ms", _report_interval_ms, 0, 5000);
        handler.item("all_messages", _all_messages);
    }
};

extern UartChannel Uart0;

extern void uartInit();
