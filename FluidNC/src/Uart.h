// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Configuration/Configurable.h"
#include "UartTypes.h"

#include "lineedit.h"
#include "Channel.h"
#include <freertos/FreeRTOS.h>  // TickType_T

class Uart : public Channel, public Configuration::Configurable {
private:
    uart_port_t _uart_num;
    Lineedit*   _lineedit;

    // One character of pushback for implementing peek().
    // We cannot use the queue for this because the queue
    // is after the check for realtime characters, whereas
    // peek() deals with characters before realtime ones
    // are handled.
    int _pushback = -1;

public:
    // These are public so that validators from classes
    // that use Uart can check that the setup is suitable.
    // E.g. some uses require an RTS pin.

    // Configurable.  Uart0 uses a fixed configuration
    int        baud     = 115200;
    UartData   dataBits = UartData::Bits8;
    UartParity parity   = UartParity::None;
    UartStop   stopBits = UartStop::Bits1;

    Pin _txd_pin;
    Pin _rxd_pin;
    Pin _rts_pin;
    Pin _cts_pin;

    Uart(int uart_num = -1, bool addCR = false);
    bool   setHalfDuplex();
    bool   setPins(int tx_pin, int rx_pin, int rts_pin = -1, int cts_pin = -1);
    void   begin();
    void   begin(unsigned long baud, UartData dataBits, UartStop stopBits, UartParity parity);
    void   begin(unsigned long baud);
    int    available(void) override;
    int    read(void) override;
    int    read(TickType_t timeout);
    size_t timedReadBytes(char* buffer, size_t length, TickType_t timeout) override;
    size_t timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    int    peek(void) override;
    size_t write(uint8_t data) override;
    size_t write(const uint8_t* buffer, size_t length) override;
    // inline size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
    // size_t        write(const char* text) override;
    void flushRx() override;
    bool flushTxTimed(TickType_t ticks);

    bool realtimeOkay(char c) override;
    bool lineComplete(char* line, char c) override;

    Channel* pollLine(char* line) override;

    int rx_buffer_available() override;

    // Configuration handlers:
    void validate() const override {
        Assert(!_txd_pin.undefined(), "UART: TXD is undefined");
        Assert(!_rxd_pin.undefined(), "UART: RXD is undefined");
        // RTS and CTS are optional.
    }

    void afterParse() override {}

    void group(Configuration::HandlerBase& handler) override {
        handler.item("txd_pin", _txd_pin);
        handler.item("rxd_pin", _rxd_pin);
        handler.item("rts_pin", _rts_pin);
        handler.item("cts_pin", _cts_pin);

        handler.item("baud", baud, 2400, 4000000);
        handler.item("mode", dataBits, parity, stopBits);
    }

    void config_message(const char* prefix, const char* usage);
};

extern Uart Uart0;

extern void uartInit();
